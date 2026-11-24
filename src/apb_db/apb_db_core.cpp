#include "apb_db_core.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "apb_db_schema.hpp"
#include "utility/apb_assert.hpp"
#include "utility/cleanup.hpp"

namespace {
// htslib sam.h bam_cigar definitions, restated so
// apb_db doesn't depend on htslib
namespace hts {
constexpr std::string_view bam_cigar_chars = "MIDNSHP=XB??????";
constexpr uint8_t bam_cigar_shift = 4;
constexpr uint32_t bam_cigar_mask = 0xf;
// 2 bits per op: 1 consumes query, 2 consumes ref
constexpr uint32_t bam_cigar_type = 0x3C1A7;
}  // namespace hts
}  // namespace

#define TRY_OK_INT(expr)                         \
  do {                                           \
    if (const auto rc = expr; rc != SQLITE_OK) { \
      return rc;                                 \
    }                                            \
  } while (false)

#define TRY_OK_EXP(expr)                         \
  do {                                           \
    if (const auto rc = expr; rc != SQLITE_OK) { \
      return std::unexpected (rc);               \
    }                                            \
  } while (false)

static int exec_sql (sqlite3* conn, const std::string_view sql)
{
  return sqlite3_exec (conn, std::string{sql}.c_str(), NULL, NULL, NULL);
};

int apb_create_schema (sqlite3* conn)
{
  TRY_OK_INT (exec_sql (conn, schema::sqlSetVersion));
  TRY_OK_INT (exec_sql (conn, schema::sqlSetFK));
  TRY_OK_INT (exec_sql (conn, schema::sqlCreateLocusTable));
  TRY_OK_INT (exec_sql (conn, schema::sqlCreateSourceTable));
  TRY_OK_INT (exec_sql (conn, schema::sqlCreateContigTable));
  TRY_OK_INT (exec_sql (conn, schema::sqlCreatePileupTable));
  TRY_OK_INT (exec_sql (conn, schema::sqlCreateSAMTable));
  TRY_OK_INT (exec_sql (conn, schema::sqlCreateUserView));

  return SQLITE_OK;
}

static int read_int_pragma (sqlite3* conn, const char* sql, int& result)
{
  sqlite3_stmt* o_stmt = NULL;
  TRY_OK_INT (sqlite3_prepare_v2 (conn, sql, -1, &o_stmt, NULL));
  Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

  if (const int rc = sqlite3_step (o_stmt); rc != SQLITE_ROW) {
    return rc;
  }

  result = sqlite3_column_int (o_stmt, 0);
  return SQLITE_OK;
}

// fingerprint of the table columns (name, type) of the database.
// Coarse; changes which don't alter the columns (views, constraints...)
// should pass fine.
static int schema_fingerprint (sqlite3* conn, std::string& result)
{
  sqlite3_stmt* o_stmt = NULL;
  if (const auto rc = sqlite3_prepare_v2 (
          conn,
          "SELECT m.name, p.name, p.type "
          "FROM sqlite_master m, pragma_table_info(m.name) p "
          "WHERE m.type = 'table' AND m.name NOT LIKE 'sqlite_%' "
          "ORDER BY m.name, p.cid;",
          -1, &o_stmt, NULL
      );
      rc != SQLITE_OK) {
    return rc;
  }
  Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

  std::string fingerprint;
  int rc;
  while ((rc = sqlite3_step (o_stmt)) == SQLITE_ROW) {
    for (int i = 0; i < 3; ++i) {
      const char* text =
          reinterpret_cast<const char*> (sqlite3_column_text (o_stmt, i));
      fingerprint += (text == nullptr) ? "" : text;
      fingerprint += (i < 2) ? ':' : '\n';
    }
  }
  if (rc != SQLITE_DONE) {
    return rc;
  }
  result = std::move (fingerprint);
  return SQLITE_OK;
}

std::expected<apb_verify_result_t, int> apb_verify_schema (sqlite3* conn)
{
  int connSchemaVersion;
  if (const auto rc = read_int_pragma (
          conn, "PRAGMA user_version;", connSchemaVersion
      );
      rc != SQLITE_OK) {
    return std::unexpected (rc);
  }
  if (connSchemaVersion == 0) {
    return apb_verify_result_t{apb_verify_result_t::versionUnset, 0};
  }
  if (connSchemaVersion != APB_SCHEMA_VER) {
    return apb_verify_result_t{
        apb_verify_result_t::verMismatch, connSchemaVersion
    };
  }
  // Verify the loaded schema matches apb's expected schema.
  sqlite3* refdb = nullptr;
  Defer refdbCleanup ([&]() { sqlite3_close_v2 (refdb); });
  if (const auto rc = sqlite3_open (":memory:", &refdb); rc != SQLITE_OK) {
    return std::unexpected (rc);
  }
  if (const auto rc = apb_create_schema (refdb); rc != SQLITE_OK) {
    return std::unexpected (rc);
  }

  std::string refPrint;
  if (const auto rc = schema_fingerprint (refdb, refPrint);
      rc != SQLITE_OK) {
    return std::unexpected (rc);
  }
  std::string testPrint;
  if (const auto rc = schema_fingerprint (conn, testPrint);
      rc != SQLITE_OK) {
    return std::unexpected (rc);
  }
  if (refPrint != testPrint) {
    return apb_verify_result_t{
        apb_verify_result_t::schemaMismatch, connSchemaVersion
    };
  }

  int fkResult;
  if (const auto rc =
          read_int_pragma (conn, "PRAGMA foreign_keys;", fkResult);
      rc != SQLITE_OK) {
    return std::unexpected (rc);
  }
  if (fkResult != 1) {
    return apb_verify_result_t{
        apb_verify_result_t::fkNotEnabled, connSchemaVersion
    };
  }

  return apb_verify_result_t{
      apb_verify_result_t::success, connSchemaVersion
  };
}


std::expected<apb_validate_result_t, int> apb_validate_db (
    sqlite3* br_conn
)
{
  {
    // NOTE: skips CHECK constraints on a read-only
    // connection. Runs other constraints, like NOT NULL, STRICT, etc.
    sqlite3_stmt* o_stmt = nullptr;
    if (const auto rc = sqlite3_prepare_v2 (
            br_conn, "PRAGMA integrity_check;", -1, &o_stmt, NULL
        );
        rc != SQLITE_OK) {
      return std::unexpected (rc);
    }
    Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

    std::string problems;
    int rc;
    while ((rc = sqlite3_step (o_stmt)) == SQLITE_ROW) {
      const auto* msg =
          reinterpret_cast<const char*> (sqlite3_column_text (o_stmt, 0));
      if (std::string_view{msg} != "ok") {
        problems += msg;
        problems += '\n';
      }
    }
    if (rc != SQLITE_DONE) {
      return std::unexpected (rc);
    }
    if (!problems.empty()) {
      return apb_validate_result_t{
          .code = apb_validate_result_t::failedIntegrityCheck,
          .problems = problems
      };
    }
  }
  {
    // FK check finds orphaned rows; not covered by integrity_check
    sqlite3_stmt* o_stmt = nullptr;
    if (const auto rc = sqlite3_prepare_v2 (
            br_conn, "PRAGMA foreign_key_check;", -1, &o_stmt, NULL
        );
        rc != SQLITE_OK) {
      return std::unexpected (rc);
    }
    Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

    // one row per violation: table, rowid, parent, fkid
    std::string problems;
    int rc;
    while ((rc = sqlite3_step (o_stmt)) == SQLITE_ROW) {
      const auto* table =
          reinterpret_cast<const char*> (sqlite3_column_text (o_stmt, 0));
      const auto* parent =
          reinterpret_cast<const char*> (sqlite3_column_text (o_stmt, 2));
      problems += table;
      problems += " rowid ";
      problems += std::to_string (sqlite3_column_int64 (o_stmt, 1));
      problems += " -> ";
      problems += parent;
      problems += '\n';
    }
    if (rc != SQLITE_DONE) {
      return std::unexpected (rc);
    }
    if (!problems.empty()) {
      return apb_validate_result_t{
          .code = apb_validate_result_t::failedForeignKeyCheck,
          .problems = problems
      };
    }
  }
  {
    // schema caps locus rows at one; check there is one
    sqlite3_stmt* o_stmt = nullptr;
    if (const auto rc = sqlite3_prepare_v2 (
            br_conn, "SELECT EXISTS (SELECT 1 FROM locus_metadata);", -1,
            &o_stmt, NULL
        );
        rc != SQLITE_OK) {
      return std::unexpected (rc);
    }
    Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

    if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_ROW) {
      return std::unexpected (rc);
    }
    if (sqlite3_column_int (o_stmt, 0) == 0) {
      return apb_validate_result_t{
          .code = apb_validate_result_t::missingLocus
      };
    }
  }
  int64_t recordsStart = 0;
  int64_t recordsEnd = 0;
  {
    // Metadata must agree with records; cross-table, so not
    // expressible as schema constraints.
    // Also REPEATS the locus_metadata CHECKs of apb_db_schema.hpp;
    // keep in sync with the schema. integrity_check skips CHECKs
    // on a read-only connection.
    // FIXME: add tests, each problem alone and combined
    // clang-format off
    constexpr std::string_view sql = R"sql(
    SELECT
      (SELECT COUNT(*) FROM sam_records r
        WHERE r.start < m.records_start OR r.end > m.records_end),
      m.n_sources,
      (SELECT COUNT(*) FROM record_sources),
      m.contig,
      m.pileup_position,
      m.records_start,
      m.records_end,
      m.ref
    FROM locus_metadata m;
    )sql";
    // clang-format on
    sqlite3_stmt* o_stmt = nullptr;
    if (const auto rc = sqlite3_prepare_v2 (
            br_conn, sql.data(), std::ssize (sql), &o_stmt, NULL
        );
        rc != SQLITE_OK) {
      return std::unexpected (rc);
    }
    Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

    if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_ROW) {
      return std::unexpected (rc);
    }
    const auto nOutsideSpan = sqlite3_column_int64 (o_stmt, 0);
    const auto nSourcesMeta = sqlite3_column_int64 (o_stmt, 1);
    const auto nSourcesActual = sqlite3_column_int64 (o_stmt, 2);
    // text lengths in bytes, as consumers index them
    const auto contigBytes = sqlite3_column_bytes (o_stmt, 3);
    const auto pos = sqlite3_column_int64 (o_stmt, 4);
    recordsStart = sqlite3_column_int64 (o_stmt, 5);
    recordsEnd = sqlite3_column_int64 (o_stmt, 6);
    const bool hasRef = sqlite3_column_type (o_stmt, 7) != SQLITE_NULL;
    const auto refBytes = sqlite3_column_bytes (o_stmt, 7);

    std::string problems;
    if (nOutsideSpan > 0) {
      problems += std::to_string (nOutsideSpan);
      problems += " records outside records_start/records_end\n";
    }
    if (nSourcesMeta != nSourcesActual) {
      problems += "n_sources is ";
      problems += std::to_string (nSourcesMeta);
      problems += " but record_sources has ";
      problems += std::to_string (nSourcesActual);
      problems += " rows\n";
    }
    if (nSourcesMeta <= 0) {
      problems += "n_sources ";
      problems += std::to_string (nSourcesMeta);
      problems += " is not positive\n";
    }
    if (contigBytes == 0) {
      problems += "contig is empty\n";
    }
    if (recordsStart <= 0 || recordsEnd < recordsStart) {
      problems += "records span ";
      problems += std::to_string (recordsStart);
      problems += "-";
      problems += std::to_string (recordsEnd);
      problems += " is invalid\n";
    }
    else {
      if (pos < recordsStart || pos > recordsEnd) {
        problems += "pileup_position ";
        problems += std::to_string (pos);
        problems += " is outside the records span\n";
      }
      // ref is indexed by position over the records span
      const auto spanLen = recordsEnd - recordsStart + 1;
      if (hasRef && refBytes != spanLen) {
        problems += "ref is ";
        problems += std::to_string (refBytes);
        problems += " bytes but the records span is ";
        problems += std::to_string (spanLen);
        problems += " bases\n";
      }
    }
    if (!problems.empty()) {
      return apb_validate_result_t{
          .code = apb_validate_result_t::metadataMismatch,
          .problems = problems
      };
    }
  }
  {
    // Per record, the CIGAR must agree with seq/qual and lie in
    // the records span. Not expressible as schema
    // constraints. Without this, consumers walking the CIGAR
    // could read out of bounds.
    constexpr std::string_view sql =
        "SELECT id, qname, start, cig32, seq, qual FROM sam_records;";
    sqlite3_stmt* o_stmt = nullptr;
    if (const auto rc = sqlite3_prepare_v2 (
            br_conn, sql.data(), std::ssize (sql), &o_stmt, NULL
        );
        rc != SQLITE_OK) {
      return std::unexpected (rc);
    }
    Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

    int rc;
    while ((rc = sqlite3_step (o_stmt)) == SQLITE_ROW) {
      const auto start = sqlite3_column_int64 (o_stmt, 2);
      // blob before bytes, per sqlite3 docs
      const auto* cig = static_cast<const unsigned char*> (
          sqlite3_column_blob (o_stmt, 3)
      );
      const auto nCig =
          static_cast<size_t> (sqlite3_column_bytes (o_stmt, 3)) /
          sizeof (uint32_t);
      int64_t qLen = 0;
      int64_t rLen = 0;
      for (size_t i = 0; i < nCig; ++i) {
        uint32_t op;
        // blob need not be aligned
        std::memcpy (&op, cig + (i * sizeof (uint32_t)), sizeof (op));
        const auto type =
            (hts::bam_cigar_type >> ((op & hts::bam_cigar_mask) << 1)) &
            3U;
        if ((type & 1U) != 0) {
          qLen += op >> hts::bam_cigar_shift;
        }
        if ((type & 2U) != 0) {
          rLen += op >> hts::bam_cigar_shift;
        }
      }

      // NULL seq/qual aren't indexed
      std::string problem;
      if (sqlite3_column_type (o_stmt, 4) != SQLITE_NULL &&
          sqlite3_column_bytes (o_stmt, 4) != qLen) {
        problem = "seq is ";
        problem += std::to_string (sqlite3_column_bytes (o_stmt, 4));
        problem += " bytes but CIGAR query length is ";
        problem += std::to_string (qLen);
      }
      else if (
          sqlite3_column_type (o_stmt, 5) != SQLITE_NULL &&
          sqlite3_column_bytes (o_stmt, 5) != qLen
      ) {
        problem = "qual is ";
        problem += std::to_string (sqlite3_column_bytes (o_stmt, 5));
        problem += " bytes but CIGAR query length is ";
        problem += std::to_string (qLen);
      }
      // overflow-safe form of start + rLen - 1 > recordsEnd
      else if (
          start < recordsStart || start > recordsEnd ||
          rLen - 1 > recordsEnd - start
      ) {
        problem = "aligned span of ";
        problem += std::to_string (rLen);
        problem += " bases from ";
        problem += std::to_string (start);
        problem += " is outside the records span";
      }
      if (!problem.empty()) {
        const auto* qname = sqlite3_column_text (o_stmt, 1);
        std::string record =
            qname != nullptr
                ? reinterpret_cast<const char*> (qname)
                : "id " +
                      std::to_string (sqlite3_column_int64 (o_stmt, 0));
        record += ": ";
        record += problem;
        record += '\n';
        return apb_validate_result_t{
            .code = apb_validate_result_t::recordMismatch,
            .problems = record
        };
      }
    }
    if (rc != SQLITE_DONE) {
      return std::unexpected (rc);
    }
  }

  return apb_validate_result_t{.code = apb_validate_result_t::success};
}

static std::string cig32_to_string (std::span<const uint32_t> cig32)
{
  // FIXME: add tests!
  std::string out;
  for (const auto& op : cig32) {
    out.append (std::to_string (op >> hts::bam_cigar_shift));
    out += hts::bam_cigar_chars[op & hts::bam_cigar_mask];
  }
  return out;
}

// insert records, with or without pileup, for a single source.
// CONVERTS FROM 0-INDEXED HALF OPEN COORDINATES TO 1-INDEXED CLOSED COORDINATES
[[nodiscard]] int apb_insert_records (
    sqlite3* conn, std::span<const apb_row1_t> recs, sqlite3_int64 sourceId
)
{
  std::unordered_map<std::string, int64_t> contigIdsByName;
  {
    sqlite3_stmt* o_stmt = NULL;
    TRY_OK_INT (sqlite3_prepare_v2 (
        conn, "SELECT id, contig FROM contigs;", -1, &o_stmt, NULL
    ));
    Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

    int rc;
    while ((rc = sqlite3_step (o_stmt)) == SQLITE_ROW) {
      const auto* name =
          reinterpret_cast<const char*> (sqlite3_column_text (o_stmt, 1));
      const int len = sqlite3_column_bytes (o_stmt, 1);
      contigIdsByName.emplace (
          std::string (name, static_cast<size_t> (len)),
          sqlite3_column_int64 (o_stmt, 0)
      );
    }
    if (rc != SQLITE_DONE) {
      return rc;
    }
  }
  sqlite3_stmt* o_stmtInsertContig;
  TRY_OK_INT (sqlite3_prepare_v2 (
      conn, schema::sqlInsertContig.data(),
      std::ssize (schema::sqlInsertContig), &o_stmtInsertContig, NULL
  ));
  Defer stmt_cleanup_contig ([&o_stmtInsertContig]() {
    return sqlite3_finalize (o_stmtInsertContig);
  });
  const int contigIdx =
      sqlite3_bind_parameter_index (o_stmtInsertContig, ":contig");

  // clang-format off
  constexpr std::string_view sqlInsertSAM = R"sql(
  INSERT INTO sam_records (
    start, cig32, seq, qual, cigar, end, qname, flag,
    mapq, mtid_id, mstart, tags, pileup_id, source_id
  ) VALUES (
    :start, :cig32, :seq, :qual, :cigar, :end, :qname, :flag,
    :mapq, :mtid_id, :mstart, :tags, :pileup_id, :source_id
  );
  )sql";
  // clang-format on
  sqlite3_stmt* o_stmtInsertSam;
  TRY_OK_INT (sqlite3_prepare_v2 (
      conn, sqlInsertSAM.data(), std::ssize (sqlInsertSAM),
      &o_stmtInsertSam, NULL
  ));
  Defer stmt_cleanup_sam ([&o_stmtInsertSam]() {
    return sqlite3_finalize (o_stmtInsertSam);
  });
  const auto samParam = [&] (const char* name) {
    return sqlite3_bind_parameter_index (o_stmtInsertSam, name);
  };
  const struct {
    int start, cig32, seq, qual, cigar, end, qname, flag, mapq, mtid_id,
        mstart, tags, pileup_id, source_id;
  } samIdx{
      .start = samParam (":start"),
      .cig32 = samParam (":cig32"),
      .seq = samParam (":seq"),
      .qual = samParam (":qual"),
      .cigar = samParam (":cigar"),
      .end = samParam (":end"),
      .qname = samParam (":qname"),
      .flag = samParam (":flag"),
      .mapq = samParam (":mapq"),
      .mtid_id = samParam (":mtid_id"),
      .mstart = samParam (":mstart"),
      .tags = samParam (":tags"),
      .pileup_id = samParam (":pileup_id"),
      .source_id = samParam (":source_id"),
  };

  // clang-format off
  constexpr std::string_view sqlInsertPileup = R"sql(
  INSERT INTO pileup_records (
    qpos, indel, is_del, is_refskip
  ) VALUES (
    :qpos, :indel, :is_del, :is_refskip
  );
  )sql";
  // clang-format on
  sqlite3_stmt* o_stmtInsertPileup;
  TRY_OK_INT (sqlite3_prepare_v2 (
      conn, sqlInsertPileup.data(), std::ssize (sqlInsertPileup),
      &o_stmtInsertPileup, NULL
  ));
  Defer stmt_cleanup_pileup ([&o_stmtInsertPileup]() {
    return sqlite3_finalize (o_stmtInsertPileup);
  });
  const auto plpParam = [&] (const char* name) {
    return sqlite3_bind_parameter_index (o_stmtInsertPileup, name);
  };
  const struct {
    int qpos, indel, is_del, is_refskip;
  } plpIdx{
      .qpos = plpParam (":qpos"),
      .indel = plpParam (":indel"),
      .is_del = plpParam (":is_del"),
      .is_refskip = plpParam (":is_refskip"),
  };

  /* bind to statement */
  // CONVERTS FROM 0-INDEXED apb_row1_t TO 1-INDEXED DB REPRESENTATION
  for (const auto& rec1 : recs) {
    TRY_OK_INT (sqlite3_bind_int64 (
        o_stmtInsertSam, samIdx.start, rec1.start_0ho + 1
    ));

    TRY_OK_INT (sqlite3_bind_blob (
        o_stmtInsertSam, samIdx.cig32, rec1.cigBlob.data(),
        static_cast<int> (rec1.cigBlob.size() << 2), SQLITE_TRANSIENT
    ));

    if (rec1.seq) {
      TRY_OK_INT (sqlite3_bind_text (
          o_stmtInsertSam, samIdx.seq, rec1.seq->data(),
          static_cast<int> (rec1.seq->size()), SQLITE_TRANSIENT
      ));
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmtInsertSam, samIdx.seq));
    }

    if (rec1.qual) {
      TRY_OK_INT (sqlite3_bind_text (
          o_stmtInsertSam, samIdx.qual, rec1.qual->data(),
          static_cast<int> (rec1.qual->size()), SQLITE_TRANSIENT
      ));
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmtInsertSam, samIdx.qual));
    }

    const auto cigStr = cig32_to_string (rec1.cigBlob);
    TRY_OK_INT (sqlite3_bind_text (
        o_stmtInsertSam, samIdx.cigar, cigStr.data(),
        static_cast<int> (cigStr.size()), SQLITE_TRANSIENT
    ));

    // 0-based half-open end == 1-based closed end
    if (rec1.end_0ho) {
      TRY_OK_INT (
          sqlite3_bind_int64 (o_stmtInsertSam, samIdx.end, *rec1.end_0ho)
      );
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmtInsertSam, samIdx.end));
    }

    if (rec1.qName) {
      TRY_OK_INT (sqlite3_bind_text (
          o_stmtInsertSam, samIdx.qname, rec1.qName->data(),
          static_cast<int> (rec1.qName->size()), SQLITE_TRANSIENT
      ));
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmtInsertSam, samIdx.qname));
    }

    if (rec1.flag) {
      TRY_OK_INT (
          sqlite3_bind_int (o_stmtInsertSam, samIdx.flag, *rec1.flag)
      );
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmtInsertSam, samIdx.flag));
    }

    if (rec1.mapQ) {
      TRY_OK_INT (
          sqlite3_bind_int (o_stmtInsertSam, samIdx.mapq, *rec1.mapQ)
      );
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmtInsertSam, samIdx.mapq));
    }

    if (rec1.mtidName) {
      int64_t contigId = -1;
      if (auto it = contigIdsByName.find (*rec1.mtidName);
          it != contigIdsByName.end()) {
        contigId = it->second;
      }
      else {
        TRY_OK_INT (sqlite3_bind_text (
            o_stmtInsertContig, contigIdx, rec1.mtidName->data(),
            static_cast<int> (rec1.mtidName->size()), SQLITE_TRANSIENT
        ));
        if (const auto rc = sqlite3_step (o_stmtInsertContig);
            rc != SQLITE_DONE) {
          return rc;
        }
        sqlite3_reset (o_stmtInsertContig);
        sqlite3_clear_bindings (o_stmtInsertContig);
        contigId = sqlite3_last_insert_rowid (conn);
        APB_ASSERT (contigId > 0, contigId);
        contigIdsByName.emplace (*rec1.mtidName, contigId);
      }

      TRY_OK_INT (
          sqlite3_bind_int64 (o_stmtInsertSam, samIdx.mtid_id, contigId)
      );
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmtInsertSam, samIdx.mtid_id));
    }

    if (rec1.mStart_0ho) {
      TRY_OK_INT (sqlite3_bind_int64 (
          o_stmtInsertSam, samIdx.mstart, rec1.mStart_0ho.value() + 1
      ));
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmtInsertSam, samIdx.mstart));
    }

    if (rec1.auxJson) {
      TRY_OK_INT (sqlite3_bind_text (
          o_stmtInsertSam, samIdx.tags, rec1.auxJson->data(),
          static_cast<int> (rec1.auxJson->size()), SQLITE_TRANSIENT
      ));
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmtInsertSam, samIdx.tags));
    }

    if (rec1.plp) {
      const auto& plp = *rec1.plp;
      TRY_OK_INT (sqlite3_bind_int64 (
          o_stmtInsertPileup, plpIdx.qpos, plp.qPos_0ho + 1
      ));
      TRY_OK_INT (
          sqlite3_bind_int (o_stmtInsertPileup, plpIdx.indel, plp.indel)
      );
      TRY_OK_INT (
          sqlite3_bind_int (o_stmtInsertPileup, plpIdx.is_del, plp.is_del)
      );
      TRY_OK_INT (sqlite3_bind_int (
          o_stmtInsertPileup, plpIdx.is_refskip, plp.is_refskip
      ));
      if (const auto rc = sqlite3_step (o_stmtInsertPileup);
          rc != SQLITE_DONE) {
        return rc;
      }
      sqlite3_reset (o_stmtInsertPileup);
      sqlite3_clear_bindings (o_stmtInsertPileup);
      auto lastId = sqlite3_last_insert_rowid (conn);
      APB_ASSERT (lastId > 0, lastId);
      TRY_OK_INT (
          sqlite3_bind_int64 (o_stmtInsertSam, samIdx.pileup_id, lastId)
      );
    }
    // else :pileup_id NULL via clear_bindings

    TRY_OK_INT (
        sqlite3_bind_int64 (o_stmtInsertSam, samIdx.source_id, sourceId)
    );

    if (const auto rc = sqlite3_step (o_stmtInsertSam);
        rc != SQLITE_DONE) {
      return rc;
    }
    sqlite3_reset (
        o_stmtInsertSam
    );  // rc mirrors the step already checked above
    sqlite3_clear_bindings (
        o_stmtInsertSam
    );  // cannot fail per sqlite3 docs
  }

  return {};
}

[[nodiscard]] int apb_insert_locus (
    sqlite3* conn, const std::string& contigName, int64_t pos_0ho,
    std::optional<std::string_view> ref
)
{
  /*
    insert the pileup locus into the database's single metadata row.
    CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
  */
  // FIXME: add tests: derived span and n_sources, ref length assert,
  // pos outside span and no records rejected
  APB_ASSERT (conn != nullptr);
  APB_ASSERT (!contigName.empty());
  APB_ASSERT (pos_0ho >= 0, pos_0ho);
  if (ref) {
    APB_ASSERT (!ref->empty());
  }

  // records span, 1-based closed as stored. NULL if no records,
  // which the schema rejects on insert; as it does pos outside the span.
  std::optional<int64_t> recordsStart;
  std::optional<int64_t> recordsEnd;
  {
    sqlite3_stmt* o_stmt;
    TRY_OK_INT (sqlite3_prepare_v2 (
        conn, "SELECT MIN(start), MAX(end) FROM sam_records;", -1, &o_stmt,
        NULL
    ));
    Defer span_cleanup ([&o_stmt]() { sqlite3_finalize (o_stmt); });
    if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_ROW) {
      return rc;
    }
    if (sqlite3_column_type (o_stmt, 0) != SQLITE_NULL) {
      recordsStart = sqlite3_column_int64 (o_stmt, 0);
    }
    if (sqlite3_column_type (o_stmt, 1) != SQLITE_NULL) {
      recordsEnd = sqlite3_column_int64 (o_stmt, 1);
    }
  }
  if (ref && recordsStart && recordsEnd) {
    APB_ASSERT (
        std::ssize (*ref) == *recordsEnd - *recordsStart + 1, ref->size(),
        *recordsStart, *recordsEnd
    );
  }

  uint16_t nSources;
  {
    sqlite3_stmt* o_stmt;
    TRY_OK_INT (sqlite3_prepare_v2 (
        conn, "SELECT COUNT(*) FROM record_sources;", -1, &o_stmt, NULL
    ));
    Defer count_cleanup ([&o_stmt]() { sqlite3_finalize (o_stmt); });
    if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_ROW) {
      return rc;
    }
    nSources = static_cast<uint16_t> (sqlite3_column_int (o_stmt, 0));
  }

  sqlite3_stmt* o_stmt;
  TRY_OK_INT (sqlite3_prepare_v2 (
      conn, schema::sqlInsertMetadata.data(),
      static_cast<int> (schema::sqlInsertMetadata.size()), &o_stmt, NULL
  ));
  Defer stmt_cleanup ([&o_stmt]() { sqlite3_finalize (o_stmt); });

  // NOTE: TIED TO sqlInsertMetadata COLUMN ORDER. BE CAREFUL!
  int col = 1;

  TRY_OK_INT (sqlite3_bind_text (
      o_stmt, col++, contigName.c_str(), -1, SQLITE_TRANSIENT
  ));
  TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col++, pos_0ho + 1));
  if (recordsStart && recordsEnd) {
    TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col++, *recordsStart));
    TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col++, *recordsEnd));
  }
  else {  // schema rejects
    TRY_OK_INT (sqlite3_bind_null (o_stmt, col++));
    TRY_OK_INT (sqlite3_bind_null (o_stmt, col++));
  }
  if (!ref) {
    TRY_OK_INT (sqlite3_bind_null (o_stmt, col++));
  }
  else {
    TRY_OK_INT (sqlite3_bind_text (
        o_stmt, col++, ref->data(), static_cast<int> (ref->size()),
        SQLITE_TRANSIENT
    ));
  }
  TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col, nSources));

  if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_DONE) {
    return rc;
  }
  return {};
}

std::expected<int, int> apb_insert_source (
    sqlite3* conn, std::string_view source, bool hasContig
)
{
  APB_ASSERT (conn != nullptr);
  APB_ASSERT (!source.empty());

  sqlite3_stmt* o_sourceInsertStmt;
  Defer source_stmt_cleanup ([&o_sourceInsertStmt]() {
    sqlite3_finalize (o_sourceInsertStmt);
  });
  TRY_OK_EXP (sqlite3_prepare_v2 (
      conn, schema::sqlInsertSource.data(),
      static_cast<int> (schema::sqlInsertSource.size()),
      &o_sourceInsertStmt, NULL
  ));
  TRY_OK_EXP (sqlite3_bind_text (
      o_sourceInsertStmt, 1, source.data(),
      static_cast<int> (source.size()), SQLITE_TRANSIENT
  ));
  TRY_OK_EXP (sqlite3_bind_int (
      o_sourceInsertStmt, 2, static_cast<bool> (hasContig)
  ));
  if (const auto rc = sqlite3_step (o_sourceInsertStmt);
      rc != SQLITE_DONE) {
    return std::unexpected (rc);
  }
  source_stmt_cleanup.invoke();
  return sqlite3_last_insert_rowid (conn);
}


/* --- TESTS --- */
#ifndef DOCTEST_CONFIG_DISABLE

#include <vector>

#include "doctest.h"
#include "fixtures.hpp"

// FIXME: untested: record with every optional unset (no pileup),
// contig dedupe across records, base/basequal NULL at refskip,
// mtid '=' for mate on the db contig (needs a metadata row)
TEST_CASE ("apb_insert_records stores records as 1-based rows")
{
  sqlite3* conn = test::new_db();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
  const auto sourceId = apb_insert_source (conn, "test.bam", true);
  REQUIRE (sourceId);

  // packed as htslib: len << shift | op (BAM_CMATCH = 0)
  constexpr uint32_t k_cig10M = 10U << hts::bam_cigar_shift;
  constexpr uint32_t k_cig6M = 6U << hts::bam_cigar_shift;

  // every optional set
  apb_row1_t full;
  full.qName = "full";
  full.flag = 99;
  full.start_0ho = 94;
  full.end_0ho = 104;
  full.mapQ = 60;
  full.plp = apb_pileup1_t{
      .qPos_0ho = 6,
      .indel = 2,
      .is_del = 0,
      .is_refskip = 0,
  };
  full.seq = "ACGTTGCAAG";
  full.qual = "ABCDEFGHIJ";  // phred 32..41
  full.cigBlob = {k_cig10M};
  full.mtidName = "chr2";
  full.mStart_0ho = 499;
  full.auxJson = R"({"NM":1})";

  // optionals empty where the view yields NULL
  apb_row1_t empty;
  empty.qName = "empty";
  empty.flag = 0;
  empty.start_0ho = 99;
  empty.end_0ho = 105;
  empty.mapQ = 0;
  empty.plp = apb_pileup1_t{
      .qPos_0ho = 0,
      .indel = 0,
      .is_del = 1,
      .is_refskip = 0,
  };
  empty.cigBlob = {k_cig6M};

  const std::vector<apb_row1_t> recs{full, empty};
  REQUIRE (apb_insert_records (conn, recs, *sourceId) == SQLITE_OK);

  sqlite3_stmt* o_stmt = nullptr;
  REQUIRE (
      sqlite3_prepare_v2 (
          conn, "SELECT * FROM reads WHERE qname = ?;", -1, &o_stmt, NULL
      ) == SQLITE_OK
  );
  Defer stmtCleanup ([&]() { sqlite3_finalize (o_stmt); });

  using Col = schema::UserReadViewSelect;
  const auto select = [&] (const char* qname) {
    sqlite3_reset (o_stmt);
    REQUIRE (
        sqlite3_bind_text (o_stmt, 1, qname, -1, SQLITE_STATIC) ==
        SQLITE_OK
    );
    REQUIRE (sqlite3_step (o_stmt) == SQLITE_ROW);
  };
  const auto isNull = [&] (int col) {
    return sqlite3_column_type (o_stmt, col) == SQLITE_NULL;
  };
  const auto asInt = [&] (int col) {
    return sqlite3_column_int64 (o_stmt, col);
  };
  const auto asText = [&] (int col) {
    return std::string (
        reinterpret_cast<const char*> (sqlite3_column_text (o_stmt, col)),
        static_cast<size_t> (sqlite3_column_bytes (o_stmt, col))
    );
  };

  SUBCASE ("full")
  {
    select ("full");
    CHECK (asInt (Col::flag) == 99);
    CHECK (asInt (Col::start) == 95);
    CHECK (asInt (Col::end) == 104);
    CHECK (asInt (Col::mapq) == 60);
    CHECK (asText (Col::base) == "C");  // seq[6]
    CHECK (asInt (Col::basequal) == 38);  // qual[6]
    CHECK (asInt (Col::qpos) == 7);
    CHECK (asInt (Col::indel) == 2);
    CHECK (asInt (Col::is_del) == 0);
    CHECK (asInt (Col::is_refskip) == 0);
    CHECK (asText (Col::cigar) == "10M");
    CHECK (asText (Col::seq) == "ACGTTGCAAG");
    CHECK (asText (Col::qual) == *full.qual);
    CHECK (asText (Col::mtid) == "chr2");
    CHECK (asInt (Col::mstart) == 500);
    CHECK (asText (Col::tags) == R"({"NM":1})");
    CHECK (sqlite3_column_bytes (o_stmt, Col::cig32) == 4);
    CHECK (asInt (Col::source_id) == *sourceId);
  }

  SUBCASE ("empty")
  {
    select ("empty");
    CHECK (asInt (Col::start) == 100);
    CHECK (asInt (Col::end) == 105);
    CHECK (asInt (Col::qpos) == 1);
    CHECK (asInt (Col::is_del) == 1);
    CHECK (asText (Col::cigar) == "6M");
    CHECK (isNull (Col::base));
    CHECK (isNull (Col::basequal));
    CHECK (isNull (Col::seq));
    CHECK (isNull (Col::qual));
    CHECK (isNull (Col::mtid));
    CHECK (isNull (Col::mstart));
    CHECK (isNull (Col::tags));
  }

  SUBCASE ("unknown source fails on foreign key")
  {
    const auto rc = apb_insert_records (conn, recs, *sourceId + 1000);
    CHECK ((rc & 0xFF) == SQLITE_CONSTRAINT);
  }
}

TEST_CASE (
    "apb_insert_source returns ids, and an error for a duplicate source"
)
{
  sqlite3* conn = test::new_db();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });

  const auto first = apb_insert_source (conn, "a.bam", true);
  REQUIRE (first);
  const auto second = apb_insert_source (conn, "b.bam", false);
  REQUIRE (second);
  CHECK (*second != *first);

  // UNIQUE (source) violation must surface as an error, not an id
  const auto dup = apb_insert_source (conn, "a.bam", true);
  REQUIRE_FALSE (dup);
  CHECK ((dup.error() & 0xFF) == SQLITE_CONSTRAINT);
}

#endif  // DOCTEST_CONFIG_DISABLE
