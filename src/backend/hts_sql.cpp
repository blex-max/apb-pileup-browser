#include "backend/hts_sql.hpp"

#include <fmt/format.h>
#include <htslib/hts.h>
#include <htslib/kstring.h>
#include <htslib/sam.h>
#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "backend/hts_types.hpp"
#include "backend/schema.hpp"
#include "shared/apb_assert.hpp"
#include "shared/cleanup.hpp"

// Error handling convention for sqlite3 calls in this file:
//
// - APB_ASSERT/APB_UNREACHABLE where statements apb builds itself, run
//   against a schema apb created or has already verified (schema
//   fingerprint match, or a fresh setup_db::init()). Any prepare/bind
//   failure, or a SQLITE_CONSTRAINT step failure, means apb's own
//   SQL/schema/bind order/invariants are corrupt and therefore a bug.
//   These states should not be reachable with valid input. Some invariants
//   are validated at multiple points - after the first validation, they
//   are considered unreachable
// - Returned/propagated error where failures are not within the control
//   of apb - i.e. user input/ouput, out of memory, etc.

// returns a fingerprint of the schema of the input database,
static std::string schema_fingerprint (sqlite3* conn)
{
  sqlite3_stmt* o_stmt = NULL;
  if (const auto rc = sqlite3_prepare_v2 (
          conn,
          "SELECT type || ':' || name || ':' || sql FROM "
          "sqlite_master "
          "WHERE sql IS NOT NULL ORDER BY type, name;",
          -1, &o_stmt, NULL
      );
      rc != SQLITE_OK) {
    APB_UNREACHABLE ("could not prepare schema fingerprint statement");
  }
  Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

  std::string fingerprint;
  int rc;
  while ((rc = sqlite3_step (o_stmt)) == SQLITE_ROW) {
    fingerprint +=
        reinterpret_cast<const char*> (sqlite3_column_text (o_stmt, 0));
    fingerprint += '\n';
  }
  if (rc != SQLITE_DONE) {
    APB_UNREACHABLE ("failure during stepping of schema fingerprint rows");
  }
  return fingerprint;
}


namespace setup_db {

sqlite3* init()
{
  // Nothing herein should fail unless miswritten, hence use of UNREACHABLE
  sqlite3* conn = nullptr;

  if (const auto rc = sqlite3_open (":memory:", &conn); rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to open in-memory database: {}", sqlite3_errstr (rc)
        )
    );
  }

  auto sqlite_exec = [&conn] (const std::string_view stmt) -> int {
    return sqlite3_exec (
        conn, std::string{stmt}.c_str(), NULL, NULL, NULL
    );
  };

  if (const auto rc = sqlite_exec (schema::sqlSetTempStoreMemory);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to set temp_store pragma: {}", sqlite3_errmsg (conn)
        )
    );
  }

  if (const auto rc = sqlite_exec (schema::sqlSetFK); rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to set foreign_key pragma: {}", sqlite3_errmsg (conn)
        )
    );
  }

  if (const auto rc = sqlite_exec (schema::sqlCreateLocusTable);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to create metadata table: {}", sqlite3_errmsg (conn)
        )
    );
  }

  if (const auto rc = sqlite_exec (schema::sqlCreateFileTable);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to create file table: {}", sqlite3_errmsg (conn)
        )
    );
  }

  if (const auto rc = sqlite_exec (schema::sqlCreatePerRecordTable);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to create per record table: {}", sqlite3_errmsg (conn)
        )
    );
  }

  if (const auto rc = sqlite_exec (schema::sqlCreateUserView);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to create reads view: {}", sqlite3_errmsg (conn)
        )
    );
  }

  return conn;
}

std::expected<sqlite3*, LoadErr> load_from_disk (std::string_view path)
{
  /*
    Copy a database file on disk into a new in-memory apb database,
    via sqlite3's online backup API.
  */
  sqlite3* conn = init();
  bool loaded = false;
  Defer connCleanup ([&]() {
    if (!loaded) {
      sqlite3_close_v2 (conn);
    }
  });

  int sqlRc = SQLITE_OK;
  sqlite3* o_fileDb = NULL;
  sqlite3_backup* o_backup = NULL;
  Defer cleanup ([&]() {
    sqlite3_backup_finish (o_backup);
    sqlite3_close_v2 (o_fileDb);
  });

  if (sqlRc = sqlite3_open_v2 (
          std::string{path}.c_str(), &o_fileDb, SQLITE_OPEN_READONLY, NULL
      );
      sqlRc != SQLITE_OK) {
    // NOTE: the error here belongs to o_fileDb (the handle
    // that failed to open), not conn.
    const std::string errMsg = sqlite3_errmsg (o_fileDb);
    return std::unexpected (
        LoadErr{.code = LoadErr::openFail, .rc = sqlRc, .msg = errMsg}
    );
  }

  if (o_backup = sqlite3_backup_init (conn, "main", o_fileDb, "main");
      o_backup == NULL) {
    sqlRc = sqlite3_errcode (conn);
    goto err_sql;
  }

  if (sqlRc = sqlite3_backup_step (o_backup, -1); sqlRc != SQLITE_DONE) {
    goto err_sql;
  }

  sqlite3_backup_finish (o_backup);
  o_backup = NULL;

  {
    // verify database is not corrupt
    sqlite3_stmt* o_stmt = NULL;
    if (const auto rc = sqlite3_prepare_v2 (
            conn, "PRAGMA quick_check;", -1, &o_stmt, NULL
        );
        rc != SQLITE_OK) {
      return std::unexpected (
          LoadErr{
              .code = LoadErr::contentCorrupt,
              .rc = std::nullopt,
              .msg = fmt::format (
                  "Could not verify database content: {}",
                  sqlite3_errmsg (conn)
              )
          }
      );
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
      problems += fmt::format (
          "Database corruption check aborted: {}\n", sqlite3_errmsg (conn)
      );
      return std::unexpected (
          LoadErr{
              .code = LoadErr::contentCorrupt,
              .rc = std::nullopt,
              .msg = problems
          }
      );
    }
  }
  {
    // Verify the loaded schema matches apb's expected schema.
    sqlite3* refdb = init();
    Defer refdbCleanup ([&]() { sqlite3_close_v2 (refdb); });

    auto refSchema = schema_fingerprint (refdb);
    auto loadedSchema = schema_fingerprint (conn);
    if (refSchema != loadedSchema) {
      return std::unexpected (
          LoadErr{
              .code = LoadErr::schemaMismatch,
              .rc = std::nullopt,
              .msg = std::nullopt
          }
      );
    }
  }

  {
    // Verify locus metadata exists and is well-formed - the one thing
    // schema matching and quick_check don't catch.
    sqlite3_stmt* o_stmt = NULL;
    if (const auto rc = sqlite3_prepare_v2 (
            conn, schema::MetaTableSelect::sql.data(),
            static_cast<int> (schema::MetaTableSelect::sql.size()),
            &o_stmt, NULL
        );
        rc != SQLITE_OK) {
      APB_UNREACHABLE (
          fmt::format (
              "failed to prepare locus metadata query: {}",
              sqlite3_errmsg (conn)
          )
      );
    }
    Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

    const auto rc = sqlite3_step (o_stmt);
    if (rc == SQLITE_DONE) {
      return std::unexpected (
          LoadErr{
              .code = LoadErr::contentCorrupt,
              .rc = std::nullopt,
              .msg = "database has no locus metadata - is this a valid "
                     "apb dump?"
          }
      );
    }
    if (rc != SQLITE_ROW) {
      APB_UNREACHABLE (
          fmt::format (
              "failed to read locus metadata row: {}",
              sqlite3_errmsg (conn)
          )
      );
    }

    query::PileupMetadata meta;
    meta.contig = {
        reinterpret_cast<const char*> (
            sqlite3_column_text (o_stmt, schema::MetaTableSelect::contig)
        ),
        static_cast<size_t> (
            sqlite3_column_bytes (o_stmt, schema::MetaTableSelect::contig)
        )
    };
    meta.pos = sqlite3_column_int64 (o_stmt, schema::MetaTableSelect::pos);
    meta.start =
        sqlite3_column_int64 (o_stmt, schema::MetaTableSelect::start);
    meta.end = sqlite3_column_int64 (o_stmt, schema::MetaTableSelect::end);
    if (sqlite3_column_type (o_stmt, schema::MetaTableSelect::ref) !=
        SQLITE_NULL) {
      meta.refSlice = std::string{
          reinterpret_cast<const char*> (
              sqlite3_column_text (o_stmt, schema::MetaTableSelect::ref)
          ),
          static_cast<size_t> (
              sqlite3_column_bytes (o_stmt, schema::MetaTableSelect::ref)
          )
      };
    }
    meta.nAln = static_cast<int16_t> (
        sqlite3_column_int (o_stmt, schema::MetaTableSelect::n_aln)
    );
    if (!meta.valid()) {
      return std::unexpected (
          LoadErr{
              .code = LoadErr::contentCorrupt,
              .rc = std::nullopt,
              .msg = "locus metadata in database is invalid or corrupt."
          }
      );
    }
  }

  loaded = true;
  return conn;

err_sql: {
  sqlite3_backup_finish (o_backup);
  o_backup = NULL;
  // NOTE: per sqlite3 docs, errors from backup_init/backup_step
  // are stored on the destination handle, so conn (the in-memory
  // connection being loaded into) is the right handle to query
  // here in every failure case above.
  const std::string errMsg = sqlite3_errmsg (conn);
  return std::unexpected (
      LoadErr{.code = LoadErr::copyFail, .rc = sqlRc, .msg = errMsg}
  );
}
}

}  // namespace setup_db


namespace query {

std::string build_where_clause (const std::vector<std::string>& fragments)
{
  if (fragments.empty()) {
    return {};
  }
  std::string out = fragments[0];
  for (size_t i = 1; i < fragments.size(); ++i) {
    out.insert (0, "(");
    out += ") ";
    out += fragments[i];
  }
  return out;
}

std::expected<sqlite3_stmt*, int> prepare_select_reads (
    sqlite3* conn, std::string_view prefix, const DynamicFragments& frags
)
{
  APB_ASSERT (prefix.back() != ';');

  sqlite3_stmt* br_stmt = nullptr;
  Defer stmt_cleanup ([&br_stmt]() { sqlite3_finalize (br_stmt); });

  std::string rsql_builtStmt{prefix};

  // build WHERE
  if (!frags.where.empty()) {
    rsql_builtStmt.append (" WHERE ");
    rsql_builtStmt.append (build_where_clause (frags.where));
  }

  // hardcoded default ordering by path
  rsql_builtStmt.append (" ORDER BY path");
  if (!frags.orderBy.empty()) {
    rsql_builtStmt.append (", ");
    rsql_builtStmt.append (frags.orderBy);
  }

  rsql_builtStmt.append (";");  // end stmt

  // If these fail, then the user statement is not valid,
  // hence they are not unreachable
  int rc;
  if (rc = sqlite3_prepare_v2 (
          conn, rsql_builtStmt.c_str(),
          static_cast<int> (rsql_builtStmt.size()), &br_stmt, NULL
      );
      rc != SQLITE_OK) {
    return std::unexpected (rc);
  }

  if (sqlite3_stmt_readonly (br_stmt) == 0) {
    return std::unexpected (SQLITE_READONLY);
  }

  stmt_cleanup.cancel();
  return br_stmt;
}

PileupMetadata get_locus_data (sqlite3* conn)
{
  sqlite3_stmt* o_stmt = NULL;
  if (const auto rc = sqlite3_prepare_v2 (
          conn, schema::MetaTableSelect::sql.data(),
          static_cast<int> (schema::MetaTableSelect::sql.size()), &o_stmt,
          NULL
      );
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to prepare locus metadata query: {}",
            sqlite3_errmsg (conn)
        )
    );
  }
  Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

  if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_ROW) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to read locus metadata row: {}", sqlite3_errmsg (conn)
        )
    );
  }

  PileupMetadata out;
  out.contig = {
      reinterpret_cast<const char*> (
          sqlite3_column_text (o_stmt, schema::MetaTableSelect::contig)
      ),
      static_cast<size_t> (
          sqlite3_column_bytes (o_stmt, schema::MetaTableSelect::contig)
      )
  };

  out.pos = sqlite3_column_int64 (o_stmt, schema::MetaTableSelect::pos);
  out.start =
      sqlite3_column_int64 (o_stmt, schema::MetaTableSelect::start);
  out.end = sqlite3_column_int64 (o_stmt, schema::MetaTableSelect::end);

  if (sqlite3_column_type (o_stmt, schema::MetaTableSelect::ref) !=
      SQLITE_NULL) {
    out.refSlice = std::string{
        reinterpret_cast<const char*> (
            sqlite3_column_text (o_stmt, schema::MetaTableSelect::ref)
        ),
        static_cast<size_t> (
            sqlite3_column_bytes (o_stmt, schema::MetaTableSelect::ref)
        )
    };
  }

  out.nAln = static_cast<int16_t> (
      sqlite3_column_int (o_stmt, schema::MetaTableSelect::n_aln)
  );

  return out;
}

std::expected<RowIterStatus, int> next_read (sqlite3_stmt* br_stmt)
{
  const int rc = sqlite3_step (br_stmt);
  if (rc == SQLITE_DONE) {
    return RowIterStatus::exhausted;
  }
  if (rc != SQLITE_ROW) {
    return std::unexpected (rc);
  }
  return RowIterStatus::rowAvail;
}

std::expected<uint32_t, int> count_rows (sqlite3_stmt* br_stmt)
{
  uint32_t nRow = 0;
  for (;; ++nRow) {
    const auto iterStatus = next_read (br_stmt);
    if (!iterStatus) {
      return std::unexpected (iterStatus.error());
    }
    if (*iterStatus == RowIterStatus::exhausted) {
      return nRow;
    }
  }
}

DiskDumpStatus dump_to_disk (sqlite3* conn, std::string_view path)
{
  int rc = SQLITE_OK;
  sqlite3* o_dumpConn = NULL;
  sqlite3_backup* o_backupConn = NULL;
  Defer dumpCleanup ([&]() {
    sqlite3_backup_finish (o_backupConn);
    sqlite3_close_v2 (o_dumpConn);
  });

  if (rc = sqlite3_open (std::string{path}.c_str(), &o_dumpConn);
      rc != SQLITE_OK) {
    goto err_sql;
  }

  if (o_backupConn =
          sqlite3_backup_init (o_dumpConn, "main", conn, "main");
      o_backupConn == NULL) {
    rc = sqlite3_errcode (o_dumpConn);
    goto err_sql;
  }

  if (rc = sqlite3_backup_step (
          o_backupConn, -1 /* copy all pages */
      );
      rc != SQLITE_DONE) {
    goto err_sql;
  }

  return {.code = DiskDumpStatus::success};

err_sql: {
  return {
      .code = DiskDumpStatus::fail,
      .sqlRc = rc,
      .dumpDbMsg = sqlite3_errmsg (o_dumpConn)
  };
}
}

std::string describe_sqlite_failure (
    int rc, std::string_view context, std::optional<std::string_view> dbMsg
)
{
  switch (rc & 0xFF) {  // strip extended result code
    case SQLITE_CANTOPEN:
      return fmt::format (
          "Failed to {}: could not open the file ({}).", context,
          sqlite3_errstr (rc)
      );
    case SQLITE_PERM:
    case SQLITE_READONLY:
      return fmt::format (
          "Failed to {}: permission denied ({}).", context,
          sqlite3_errstr (rc)
      );
    case SQLITE_NOTADB:
    case SQLITE_CORRUPT:
      return fmt::format (
          "Failed to {}: the file is not a valid sqlite3 "
          "database, or is corrupt ({}).",
          context, sqlite3_errstr (rc)
      );
    case SQLITE_FULL:
    case SQLITE_IOERR:
      return fmt::format (
          "Failed to {}: a disk I/O error occurred ({}).", context,
          sqlite3_errstr (rc)
      );
    case SQLITE_NOMEM:
      return fmt::format (
          "Failed to {}: out of memory. Were you trying to load a very "
          "large pileup?",
          context
      );
    default:
      return fmt::format (
          "Failed to {}, reporting code {} and status {} - "
          "please report this failure to the maintainer.",
          context, rc, dbMsg.value_or (sqlite3_errstr (rc))
      );
  }
}

}  // namespace query

namespace hts2sql {

/* -- internal helpers -- */

/* TAG CONVERSION */

// Escape a raw aux string value for embedding in a JSON string literal.
// SAM 'A'/'Z' values are drawn from [ !-~]+, which permits '"' and '\'
// unescaped. Thererfore without this, valid tags can produce malformed JSON
// and trip the `reads.tags` CHECK(json_valid(tags)) constraint.
static void append_json_escaped (
    const char* br_data, size_t len, std::string& out
)
{
  static const char hexDigits[] = "0123456789abcdef";
  for (size_t i = 0; i < len; ++i) {
    const auto ch = static_cast<unsigned char> (br_data[i]);
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      default:
        if (ch < 0x20) {
          out += "\\u00";
          out += hexDigits[(ch >> 4) & 0xF];
          out += hexDigits[ch & 0xF];
        }
        else {
          out += static_cast<char> (ch);
        }
    }
  }
}

struct Aux1ToJsonErr {
  enum Codes : uint8_t {
    parseFail,
  };
};
// converts single aux tag to json entry
// returns unexpected on failure to parse aux tag.
static std::expected<std::string, Aux1ToJsonErr::Codes> aux1_to_json (
    const uint8_t* aux1Start, const uint8_t* aux1End
)
{
  kstring_t o_kstr;
  ks_initialize (&o_kstr);
  Defer kstrCleanup ([&]() { ks_free (&o_kstr); });
  if (sam_format_aux1 (
          aux1Start - 2, *aux1Start, aux1Start + 1, aux1End, &o_kstr
      ) == NULL) {
    return std::unexpected (Aux1ToJsonErr::Codes::parseFail);
  }
  const char* br_str = ks_str (&o_kstr);

  /* append key */
  std::string out{'"'};  // open key quotes
  out.append (br_str, 2);  // 2-ch tag
  out += '"';  // close
  out += ':';  // add key-val separator

  /* append val */
  const char typeCh = *(br_str + 3);
  if (typeCh == 'B') {
    // array aux tag
    out += '[';  // open JSON array
    // form "TAG:B:<subtype>" (2 + 1 + 2 + 1 = 6 chars);
    constexpr auto arrayTagPrefixLen = 6;
    if (ks_len (&o_kstr) > arrayTagPrefixLen) {
      const char* payloadStartPtr = br_str + arrayTagPrefixLen + 1;
      // all allowed array types are numeric
      // no need to check type
      ks_tokaux_t tokAux;
      const char* tok;
      bool firstTok = true;
      for (tok = kstrtok (payloadStartPtr, ",", &tokAux); tok != nullptr;
           tok = kstrtok (NULL, NULL, &tokAux)) {
        const size_t tokLen = static_cast<size_t> (tokAux.p - tok);
        if (!firstTok) {
          out += ',';
        }
        out.append (tok, tokLen);
        firstTok = false;
      }
    }
    out += ']';  // close JSON array
  }
  else {
    constexpr auto tagPrefixLen = 5;
    const auto* payloadStartPtr = br_str + tagPrefixLen;
    const size_t payloadLen = ks_len (&o_kstr) - tagPrefixLen;
    switch (typeCh) {
      case 'A':
      case 'Z':
      case 'H':
        // payload as string
        out += '"';
        append_json_escaped (payloadStartPtr, payloadLen, out);
        out += '"';
        break;
      default:
        // payload as numeric
        out.append (payloadStartPtr, payloadLen);
        break;
    }
  }

  return out;
}

// Convert a bam_pileup1_t
// into the pileup database
// interface type.
//
// NOTE: takes mTidName directly to
// avoid dealing with SAM header.
static bool fill_fields (
    PileupFields& pf, const bam_pileup1_t* br_p1, const char* mTidName
)
{
  const auto* br_b1 = br_p1->b;
  const auto nCig = br_b1->core.n_cigar;
  const auto* br_cig = bam_get_cigar (br_b1);

  pf.qPos = br_p1->qpos;
  pf.start = br_b1->core.pos;
  if (br_b1->core.mpos >= 0) {  // <0 == unaligned (or no mate)
    pf.mStart = br_b1->core.mpos;
  }

  pf.indel = br_p1->indel;
  pf.isDel = br_p1->is_del;
  pf.isHead = br_p1->is_head;
  pf.isTail = br_p1->is_tail;
  pf.isRefSkip = br_p1->is_refskip;
  // NOTE: qname null terminated,
  // so assignment safe.
  pf.qName = bam_get_qname (br_b1);
  pf.flag = br_b1->core.flag;
  pf.mapQ = br_b1->core.qual;
  if (mTidName != NULL) {
    pf.mtidName = mTidName;
  }
  pf.rawCig = {br_cig, br_cig + nCig};
  pf.nCig = br_b1->core.n_cigar;

  {
    /* stringify seq, qual */
    const auto lq = static_cast<size_t> (br_b1->core.l_qseq);
    // Tests per sam format spec
    const bool hasSeq = lq > 0;
    const bool hasQual = hasSeq && bam_get_qual (br_b1)[0] != 0xFF;
    if (hasSeq) {
      pf.seq.emplace (lq, ' ');

      const uint8_t* br_qs = bam_get_seq (br_b1);
      for (size_t j = 0; j < lq; ++j) {
        (*pf.seq)[j] = seq_nt16_str[bam_seqi (br_qs, j)];
      }
      if (!br_p1->is_del) {
        pf.base = (*pf.seq)[static_cast<size_t> (br_p1->qpos)];
      }
    }
    if (hasQual) {
      pf.rawQual.emplace (lq);
      const uint8_t* br_qq = bam_get_qual (br_b1);
      for (size_t j = 0; j < lq; ++j) {
        (*pf.rawQual)[j] = br_qq[j];
      }
      if (!br_p1->is_del) {
        pf.baseQual = br_qq[br_p1->qpos];
      }
    }
    // else these fields are nullopt
  }

  {
    /* stringify cigar */
    // ASSUMPTION: cigar available and correct.
    pf.cig = stringify_cigar (br_cig, nCig);
    pf.end = pf.start + bam_cigar2rlen (static_cast<int> (nCig), br_cig);
  }

  {
    /* aux to json; left empty if no aux tags present */
    auto& auxJson = pf.auxJson;
    auxJson.clear();
    const uint8_t* br_dataEnd = br_b1->data + br_b1->l_data;
    const uint8_t* br_aux1 = bam_aux_first (br_b1);
    if (br_aux1 != NULL) {
      auxJson += '{';
      for (; br_aux1 != nullptr;) {
        auto tagEntry = aux1_to_json (br_aux1, br_dataEnd);
        if (!tagEntry) {
          return false;
        }
        auxJson += *tagEntry;
        br_aux1 = bam_aux_next (br_b1, br_aux1);
        if (br_aux1 == NULL) {
          break;
        }
        auxJson += ',';
      }
      auxJson += '}';
    }
  }

  APB_ASSERT (pf.valid(), pf);
  return true;
}

// -- public API -- //

InsertPileupStatus insert_pileup (
    sqlite3* conn, const bam_pileup1_t* br_plpArr, const size_t nPlp,
    const std::string& contigName, sqlite3_int64 aln_id,
    const Tid2StrFn& mtid2name
)
{
  APB_ASSERT (br_plpArr != nullptr);
  APB_ASSERT (!contigName.empty());

  auto* o_stmt = prepare_insert_reads_stmt (conn);
  Defer stmt_cleanup ([&o_stmt]() { return sqlite3_finalize (o_stmt); });

  // manually begin/commit transaction.
  if (const auto rc = sqlite3_exec (conn, "BEGIN;", NULL, NULL, NULL);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to begin transaction: {}", sqlite3_errmsg (conn)
        )
    );
  }

  for (size_t i = 0; i < nPlp; ++i) {
    PileupFields readI;
    const auto* p1 = &br_plpArr[i];

    const char* mtidName = NULL;
    {
      /* stringify mtid, if available */
      // '=' if same contig as this read, per SAM RNEXT convention;
      // NULL if no mate reference (core.mtid < 0).
      const auto* b1 = p1->b;
      if (b1->core.mtid >= 0) {
        // NOTE: in the case where tid2name
        // fails, null recorded in database.
        // Hence failure not checked.
        mtidName = (b1->core.mtid == b1->core.tid)
                       ? "="
                       : mtid2name (b1->core.mtid);
      }
      else {
        mtidName = NULL;
      }
    }

    if (!fill_fields (readI, p1, mtidName)) {
      return InsertPileupStatus{
          .code = InsertPileupStatus::auxParseFail,
          .rc = std::nullopt /* no specific code */
      };
    }

    bind_read_data (o_stmt, readI, aln_id);

    if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_DONE) {
      if ((rc & 0xFF) == SQLITE_CONSTRAINT) {
        APB_UNREACHABLE (
            fmt::format (
                "read insert violated a schema constraint: {}",
                sqlite3_errmsg (conn)
            )
        );
      }
      return InsertPileupStatus{
          .code = InsertPileupStatus::sqlFail, .rc = rc
      };
    }
    sqlite3_reset (o_stmt);  // rc mirrors the step already checked above
    sqlite3_clear_bindings (o_stmt);  // cannot fail per sqlite3 docs
  }

  if (const auto rc = sqlite3_exec (conn, "COMMIT;", NULL, NULL, NULL);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to commit transaction: {}", sqlite3_errmsg (conn)
        )
    );
  }
  return {};
};

[[nodiscard]] int insert_metadata (
    sqlite3* conn, const std::string& contigName, int64_t pileupPos,
    const GenomicSpan& pileupSpan,
    const std::optional<std::string>& refSlice, int16_t n_aln
)
{
  /*
    insert the pileup locus into the database's single metadata row.
    Uses automatic transaction handling, not necessary to begin/end transaction.

    CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
  */
  APB_ASSERT (!contigName.empty());
  APB_ASSERT (pileupSpan.valid(), pileupSpan);
  APB_ASSERT (pileupPos >= 0, pileupPos);
  APB_ASSERT (pileupPos >= pileupSpan.start, pileupPos, pileupSpan);
  APB_ASSERT (pileupPos <= pileupSpan.end, pileupPos, pileupSpan);

  sqlite3_stmt* o_stmt;
  if (const auto rc = sqlite3_prepare_v2 (
          conn, schema::sqlInsertMetadata.data(),
          static_cast<int> (schema::sqlInsertMetadata.size()), &o_stmt,
          NULL
      );
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to prepare metadata insert statement: {}",
            sqlite3_errmsg (conn)
        )
    );
  }
  Defer stmt_cleanup ([&o_stmt]() { sqlite3_finalize (o_stmt); });

  // NOTE: TIED TO SCHEMA ORDER. BE CAREFUL!
  int col = 1;
  auto bindOK = [&] (int rc) {
    if (rc != SQLITE_OK) {
      APB_UNREACHABLE (
          fmt::format ("bind failed: {}", sqlite3_errstr (rc))
      );
    }
  };

  bindOK (sqlite3_bind_int64 (o_stmt, col++, kSchemaVersion));
  bindOK (sqlite3_bind_text (
      o_stmt, col++, contigName.c_str(), -1, SQLITE_TRANSIENT
  ));
  bindOK (sqlite3_bind_int64 (o_stmt, col++, pileupPos + 1));
  bindOK (sqlite3_bind_int64 (o_stmt, col++, pileupSpan.start + 1));
  bindOK (sqlite3_bind_int64 (o_stmt, col++, pileupSpan.end));
  if (!refSlice) {
    bindOK (sqlite3_bind_null (o_stmt, col++));
  }
  else {
    APB_ASSERT (!(*refSlice).empty());
    bindOK (sqlite3_bind_text (
        o_stmt, col++, (*refSlice).c_str(),
        static_cast<int> ((*refSlice).size()), SQLITE_TRANSIENT
    ));
  }
  bindOK (sqlite3_bind_int64 (o_stmt, col, n_aln));

  if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_DONE) {
    if ((rc & 0xFF) == SQLITE_CONSTRAINT) {
      APB_UNREACHABLE (
          fmt::format (
              "metadata insert violated a schema constraint: {}",
              sqlite3_errmsg (conn)
          )
      );
    }
    return rc;
  }
  return 0;
}

sqlite3_stmt* prepare_insert_reads_stmt (sqlite3* conn)
{
  sqlite3_stmt* br_stmt;
  if (const auto rc = sqlite3_prepare_v2 (
          conn, schema::sqlInsertReads.data(),
          static_cast<int> (schema::sqlInsertReads.size()), &br_stmt, NULL
      );
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to prepare insert statement for per-record data "
            "table: {}",
            sqlite3_errmsg (conn)
        )
    );
  }
  return br_stmt;
}

// Bind one pileup row's fields into `stmt`, in column order matching
// stmt_str_InsertReads. Binding does not evaluate table constraints
// so bind failures are always an invariant violation.
//
// CONVERTS FROM 0-INDEXED PileupFields TO 1-INDEXED DB REPRESENTATION
void bind_read_data (
    sqlite3_stmt* stmt, const PileupFields& pf, sqlite3_int64 aln_id
)
{
  APB_ASSERT (pf.valid(), pf);

  // INSERTION ORDER TIED TO SCHEMA; BE CAREFUL! (schema.hpp)
  int col = 1;
  auto bindOK = [&] (int rc) {
    if (rc != SQLITE_OK) {
      APB_UNREACHABLE (
          fmt::format ("bind failed: {}", sqlite3_errstr (rc))
      );
    }
  };

  bindOK (sqlite3_bind_text (
      stmt, col++, pf.qName.data(), static_cast<int> (pf.qName.size()),
      SQLITE_TRANSIENT
  ));
  bindOK (sqlite3_bind_int (stmt, col++, pf.flag));
  bindOK (sqlite3_bind_int64 (stmt, col++, pf.start + 1));
  bindOK (sqlite3_bind_int64 (stmt, col++, pf.end));
  bindOK (sqlite3_bind_int (stmt, col++, pf.mapQ));

  if (pf.base) {
    bindOK (
        sqlite3_bind_text (stmt, col++, &(*pf.base), 1, SQLITE_TRANSIENT)
    );
  }
  else {
    bindOK (sqlite3_bind_null (stmt, col++));
  }

  if (pf.baseQual) {
    bindOK (sqlite3_bind_int (stmt, col++, *pf.baseQual));
  }
  else {
    bindOK (sqlite3_bind_null (stmt, col++));
  }

  bindOK (sqlite3_bind_int64 (stmt, col++, pf.qPos + 1));
  bindOK (sqlite3_bind_int (stmt, col++, pf.indel));
  bindOK (sqlite3_bind_int (stmt, col++, static_cast<int> (pf.isDel)));
  bindOK (sqlite3_bind_int (stmt, col++, static_cast<int> (pf.isHead)));
  bindOK (sqlite3_bind_int (stmt, col++, static_cast<int> (pf.isTail)));
  bindOK (sqlite3_bind_int (stmt, col++, static_cast<int> (pf.isRefSkip)));
  bindOK (sqlite3_bind_text (
      stmt, col++, pf.cig.data(), static_cast<int> (pf.cig.size()),
      SQLITE_TRANSIENT
  ));

  if (pf.seq) {
    bindOK (sqlite3_bind_text (
        stmt, col++, pf.seq->data(), static_cast<int> (pf.seq->size()),
        SQLITE_TRANSIENT
    ));
  }
  else {
    bindOK (sqlite3_bind_null (stmt, col++));
  }

  if (pf.rawQual) {
    bindOK (sqlite3_bind_blob (
        stmt, col++, pf.rawQual->data(),
        static_cast<int> (pf.rawQual->size()), SQLITE_TRANSIENT
    ));
  }
  else {
    bindOK (sqlite3_bind_null (stmt, col++));
  }

  if (pf.mtidName) {
    bindOK (sqlite3_bind_text (
        stmt, col++, pf.mtidName->data(),
        static_cast<int> (pf.mtidName->size()), SQLITE_TRANSIENT
    ));
  }
  else {
    bindOK (sqlite3_bind_null (stmt, col++));
  }

  if (pf.mStart) {
    bindOK (sqlite3_bind_int64 (stmt, col++, pf.mStart.value() + 1));
  }
  else {
    bindOK (sqlite3_bind_null (stmt, col++));
  }

  if (pf.auxJson.empty()) {
    bindOK (sqlite3_bind_null (stmt, col++));
  }
  else {
    bindOK (sqlite3_bind_text (
        stmt, col++, pf.auxJson.data(),
        static_cast<int> (pf.auxJson.size()), SQLITE_TRANSIENT
    ));
  }

  bindOK (sqlite3_bind_blob (
      stmt, col++, pf.rawCig.data(), static_cast<int> (pf.nCig << 2),
      SQLITE_TRANSIENT
  ));
  bindOK (sqlite3_bind_int (stmt, col++, static_cast<int> (pf.nCig)));
  bindOK (sqlite3_bind_int64 (stmt, col++, aln_id));
}

std::string stringify_cigar (const uint32_t* br_cig, size_t nCig)
{
  std::string out;
  for (size_t opi = 0; opi < nCig; opi++) {
    const auto cigel = br_cig[opi];
    out += std::to_string (bam_cigar_oplen (cigel));
    out += bam_cigar_opchr (cigel);
  }
  return out;
}

}  // namespace hts2sql

#ifndef DOCTEST_CONFIG_DISABLE

#include "doctest.h"

// NOTE:
// The point of these tests is to catch bugs which will not
// be trivially caught by the defensive programming (APB_ASSERT/UNREACHABLE etc.)
// during test runs. So rare cases only rather than general behaviour verification.

/* ---- TESTS append_json_escaped ---- */

TEST_CASE ("append_json_escaped escapes per JSON string rules")
{
  SUBCASE ("quote and backslash")
  {
    std::string out;
    const std::string in = "A\"B";
    hts2sql::append_json_escaped (in.data(), in.size(), out);
    CHECK (out == "A\\\"B");

    out.clear();
    const std::string in2 = "A\\B";
    hts2sql::append_json_escaped (in2.data(), in2.size(), out);
    CHECK (out == "A\\\\B");
  }

  SUBCASE ("control characters escape to \\u00XX")
  {
    std::string out;
    const char in[] = {static_cast<char> (0x01)};
    hts2sql::append_json_escaped (in, 1, out);
    CHECK (out == "\\u0001");

    out.clear();
    const char in2[] = {static_cast<char> (0x1f)};
    hts2sql::append_json_escaped (in2, 1, out);
    CHECK (out == "\\u001f");
  }

  SUBCASE (
      "printable ASCII and non-ASCII bytes pass through "
      "unchanged"
  )
  {
    std::string out;
    const std::string in = "hello";
    hts2sql::append_json_escaped (in.data(), in.size(), out);
    CHECK (out == "hello");

    out.clear();
    const char in2[] = {static_cast<char> (0xC3)};
    hts2sql::append_json_escaped (in2, 1, out);
    REQUIRE (out.size() == 1);
    CHECK (static_cast<unsigned char> (out[0]) == 0xC3);
  }

  SUBCASE ("empty input appends nothing")
  {
    std::string out = "unchanged";
    hts2sql::append_json_escaped (nullptr, 0, out);
    CHECK (out == "unchanged");
  }
}

// ---- TESTS aux1_to_json ---- //
TEST_CASE ("aux1_to_json escapes quote and backslash in string tags")
{
  SUBCASE ("string value containing '\"' and '\\' gets escaped")
  {
    uint8_t buf[] = {'Z', 'Z', 'Z', 'a', '"', 'b', '\\', 'c', '\0'};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);

    std::string expectedVal = "a";
    expectedVal += '\\';
    expectedVal += '"';
    expectedVal += "b";
    expectedVal += '\\';
    expectedVal += '\\';
    expectedVal += "c";
    std::string expected = "\"ZZ\":\"";
    expected += expectedVal;
    expected += "\"";
    CHECK (*r == expected);
  }
}

TEST_CASE (
    "aux1_to_json handles zero-, one-, and multi-element 'B' "
    "arrays"
)
{
  SUBCASE ("zero elements")
  {
    uint8_t buf[] = {'X', 'Y', 'B', 'c', 0, 0, 0, 0};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XY\":[]");
  }

  SUBCASE ("single element")
  {
    uint8_t buf[] = {'X', 'Y', 'B', 'c', 1, 0, 0, 0, 5};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XY\":[5]");
  }

  SUBCASE ("three elements")
  {
    uint8_t buf[] = {'X', 'Y', 'B', 'c', 3, 0, 0, 0, 1, 2, 3};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XY\":[1,2,3]");
  }
}

TEST_CASE ("aux1_to_json formats float, hex and awkward string tags")
{
  const auto floatTag = [] (float f) {
    uint8_t buf[7] = {'X', 'F', 'f'};
    std::memcpy (buf + 3, &f, sizeof (f));
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    return *r;
  };

  SUBCASE ("finite float") { CHECK (floatTag (1.5f) == "\"XF\":1.5"); }

  SUBCASE ("float array")
  {
    uint8_t buf[] = {'X', 'F', 'B', 'f', 2, 0, 0,  0,
                     0,   0,   192, 63,  0, 0, 32, 64};  // 1.5f, 2.5f
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XF\":[1.5,2.5]");
  }

  SUBCASE ("empty string")
  {
    uint8_t buf[] = {'Z', 'E', 'Z', '\0'};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"ZE\":\"\"");
  }

  SUBCASE ("hex string")
  {
    uint8_t buf[] = {'H', 'X', 'H', '1', 'A', '\0'};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"HX\":\"1A\"");
  }

  SUBCASE ("control character in string")
  {
    uint8_t buf[] = {'C', 'C', 'Z', 'a', 0x01, 'b', '\0'};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"CC\":\"a\\u0001b\"");
  }
}

namespace test {

// alignment_files row that per-record inserts reference via path_id.
constexpr sqlite3_int64 k_testFileId = 1;

void seed_file (sqlite3* conn)
{
  const char* sql =
      "INSERT INTO alignment_files (id, path, has_contig, has_reads) "
      "VALUES (1, 'test.bam', 1, 1);";
  REQUIRE (
      sqlite3_exec (conn, sql, nullptr, nullptr, nullptr) == SQLITE_OK
  );
}

}  // namespace test

/* ---- TESTS build_where_clause / prepare_select_reads ---- */

namespace test {

// seed a fresh db with locus metadata and three reads
void seed_reads (
    sqlite3* conn, int16_t nAln = 1,
    std::optional<std::string> refSlice = std::nullopt
)
{
  seed_file (conn);
  REQUIRE (
      hts2sql::insert_metadata (
          conn, "chr1", 149, GenomicSpan{.start = 99, .end = 200},
          std::move (refSlice), nAln
      ) == SQLITE_OK
  );

  auto stmt = hts2sql::prepare_insert_reads_stmt (conn);

  auto row = [] (std::string qName, uint16_t flag, uint8_t mapQ,
                 hts_pos_t start, hts_pos_t end) {
    hts2sql::PileupFields pf{};
    pf.qName = std::move (qName);
    pf.flag = flag;
    pf.start = start - 1;  // 0-indexed input -> db start
    pf.end = end;  // unshifted -> db end
    pf.mapQ = mapQ;
    pf.base = 'A';
    pf.baseQual = 30;
    pf.qPos = 0;
    pf.indel = 0;
    pf.isDel = false;
    pf.isHead = false;
    pf.isTail = false;
    pf.isRefSkip = false;
    pf.cig = "1M";
    pf.seq = "A";
    pf.rawQual = std::vector<uint8_t> (1, 30);
    pf.auxJson = "";
    pf.rawCig = {static_cast<uint32_t> (bam_cigar_gen (1, BAM_CMATCH))};
    pf.nCig = pf.rawCig.size();
    return pf;
  };

  // (qName, flag, mapQ, start, end) -- all satisfy
  // 100 <= start <= 150 <= end <= 200.
  const std::vector<hts2sql::PileupFields> rows{
      row ("readA", 99, 60, 100, 170),
      row ("readB", 4, 10, 120, 190),
      row ("readC", 99, 45, 140, 200),
  };
  for (const auto& pf : rows) {
    hts2sql::bind_read_data (stmt, pf, k_testFileId);
    REQUIRE (sqlite3_step (stmt) == SQLITE_DONE);
    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
  }
}

}  // namespace test

TEST_CASE ("build_where_clause left-wraps fragments in parens")
{
  CHECK (query::build_where_clause ({}) == "");
  CHECK (query::build_where_clause ({"flag = 99"}) == "flag = 99");

  CHECK (
      query::build_where_clause ({"a", "OR b", "AND c"}) ==
      "((a) OR b) AND c"
  );
}

TEST_CASE ("prepare_select_reads filters on WHERE fragments")
{
  sqlite3* conn = setup_db::init();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
  test::seed_reads (conn);

  SUBCASE ("multiple fragments")
  {
    query::DynamicFragments frags{
        .where = {"flag = 99", "AND mapq > 50"}, .orderBy = ""
    };
    auto stmtRet = query::prepare_select_reads (
        conn, schema::UserReadViewSelect::sqlPrefix, frags
    );
    REQUIRE (stmtRet);
    auto stmt{std::move (*stmtRet)};
    auto countRet = query::count_rows (stmt);
    REQUIRE (countRet);
    CHECK (*countRet == 1);  // readA only (flag 99, mapq 60)
  }
}

/* ---- TESTS insert_pileup ---- */

namespace test {

// Hands pre-parsed records, in order, to htslib's pileup engine.
struct RecordFeed {
  const std::vector<bam1_t*>* recs;
  size_t next = 0;
};
extern "C" {
int next_fed_record (void* data, bam1_t* b)
{
  auto* feed = static_cast<RecordFeed*> (data);
  if (feed->next >= feed->recs->size()) {
    return -1;
  }
  return bam_copy1 (b, (*feed->recs)[feed->next++]) == NULL ? -2 : 0;
}
}

// Expected stored values. 1-based, as in the db.
struct ExpectedRead {
  int64_t start, end;
  std::string cigar;
  int ncig;
  std::optional<int> qpos = std::nullopt;  // not checked if nullopt
  std::optional<char> base = std::nullopt;  // nullopt: expect NULL
  std::optional<int> basequal = std::nullopt;  // nullopt: expect NULL
  bool hasSeq = true;
  bool hasQual = true;
  // not checked if nullopt
  std::optional<int> indel = std::nullopt;
  std::optional<int> isDel = std::nullopt;
  std::optional<int> isHead = std::nullopt;
  std::optional<int> isTail = std::nullopt;
  std::optional<int> isRefSkip = std::nullopt;
  std::optional<std::string> mtid = std::nullopt;  // nullopt: expect NULL
  std::optional<int64_t> mstart = std::nullopt;  // nullopt: expect NULL
};

}  // namespace test

TEST_CASE ("insert_pileup stores edge-case reads piled up by htslib")
{
  // Reads are parsed from SAM text and piled up by htslib itself, so
  // qpos/is_del/is_head etc. are as the real program sees them.
  // Pileup position is chr1:100 (1-based).
  // Seq/qual are prefixes of the strings below, so the base and quality
  // at each index are distinct: basequal at index i is 32 + i.
  const std::string kSeq = "ACGTTGCAAGCT";
  const std::string kQual = "ABCDEFGHIJKL";
  const auto seq = [&] (size_t n) { return kSeq.substr (0, n); };
  const auto qual = [&] (size_t n) { return kQual.substr (0, n); };
  const auto samLine = [] (std::string_view qname, int flag, int pos,
                           std::string_view cigar, std::string_view rnext,
                           int pnext, std::string_view sq,
                           std::string_view ql) {
    return fmt::format (
        "{}\t{}\tchr1\t{}\t60\t{}\t{}\t{}\t0\t{}\t{}", qname, flag, pos,
        cigar, rnext, pnext, sq, ql
    );
  };

  struct Case {
    std::string qname;
    std::string sam;
    test::ExpectedRead expect;
  };
  // coordinate sorted, as htslib's pileup requires.
  const std::vector<Case> cases{
      {"normal",
       samLine ("normal", 99, 95, "10M", "chr2", 500, seq (10), qual (10)),
       {.start = 95,
        .end = 104,
        .cigar = "10M",
        .ncig = 1,
        .qpos = 6,
        .base = 'G',
        .basequal = 37,
        .isHead = 0,
        .isTail = 0,
        .mtid = "chr2",
        .mstart = 500}},
      {"deletion",
       samLine ("deletion", 0, 95, "4M2D6M", "*", 0, seq (10), qual (10)),
       {.start = 95,
        .end = 106,
        .cigar = "4M2D6M",
        .ncig = 3,
        .isDel = 1}},
      {"refskip",
       samLine ("refskip", 0, 95, "4M2N6M", "*", 0, seq (10), qual (10)),
       {.start = 95,
        .end = 106,
        .cigar = "4M2N6M",
        .ncig = 3,
        .isRefSkip = 1}},
      {"insafter",
       samLine ("insafter", 0, 95, "6M2I4M", "*", 0, seq (12), qual (12)),
       {.start = 95,
        .end = 104,
        .cigar = "6M2I4M",
        .ncig = 3,
        .qpos = 6,
        .base = 'G',
        .basequal = 37,
        .indel = 2}},
      {"eqx",
       samLine ("eqx", 0, 95, "3=1X6=", "*", 0, seq (10), qual (10)),
       {.start = 95,
        .end = 104,
        .cigar = "3=1X6=",
        .ncig = 3,
        .qpos = 6,
        .base = 'G',
        .basequal = 37}},
      {"qualstar",
       samLine ("qualstar", 0, 96, "8M", "*", 0, seq (8), "*"),
       {.start = 96,
        .end = 103,
        .cigar = "8M",
        .ncig = 1,
        .qpos = 5,
        .base = 'T',
        .hasQual = false,
        .mtid = std::nullopt}},
      {"hardclip",
       samLine ("hardclip", 0, 96, "2H8M3H", "*", 0, seq (8), qual (8)),
       {.start = 96,
        .end = 103,
        .cigar = "2H8M3H",
        .ncig = 3,
        .qpos = 5,
        .base = 'T',
        .basequal = 36}},
      {"tail",
       samLine ("tail", 0, 96, "5M", "*", 0, seq (5), qual (5)),
       {.start = 96,
        .end = 100,
        .cigar = "5M",
        .ncig = 1,
        .qpos = 5,
        .base = 'T',
        .basequal = 36,
        .isHead = 0,
        .isTail = 1}},
      {"seqstar",
       samLine ("seqstar", 0, 97, "6M", "*", 0, "*", "*"),
       {.start = 97,
        .end = 102,
        .cigar = "6M",
        .ncig = 1,
        .qpos = 4,
        .hasSeq = false,
        .hasQual = false}},
      {"softclip",
       samLine ("softclip", 0, 98, "3S5M2S", "*", 0, seq (10), qual (10)),
       {.start = 98,
        .end = 102,
        .cigar = "3S5M2S",
        .ncig = 3,
        .qpos = 6,
        .base = 'G',
        .basequal = 37}},
      {"head",
       samLine ("head", 0, 100, "5M", "*", 0, seq (5), qual (5)),
       {.start = 100,
        .end = 104,
        .cigar = "5M",
        .ncig = 1,
        .qpos = 1,
        .base = 'A',
        .basequal = 32,
        .isHead = 1,
        .isTail = 0}},
      {"onebase",
       samLine ("onebase", 0, 100, "1M", "*", 0, "G", "5"),
       {.start = 100,
        .end = 100,
        .cigar = "1M",
        .ncig = 1,
        .qpos = 1,
        .base = 'G',
        .basequal = 20,
        .isHead = 1,
        .isTail = 1}},
  };

  /* parse records */
  const std::string hdrText =
      "@HD\tVN:1.6\tSO:coordinate\n@SQ\tSN:chr1\tLN:1000\n"
      "@SQ\tSN:chr2\tLN:1000\n";
  sam_hdr_t* hdr = sam_hdr_parse (hdrText.size(), hdrText.c_str());
  REQUIRE (hdr != nullptr);
  Defer hdrCleanup ([&]() { sam_hdr_destroy (hdr); });

  std::vector<bam1_t*> recs;
  Defer recsCleanup ([&]() {
    for (auto* r : recs) {
      bam_destroy1 (r);
    }
  });
  kstring_t ks = KS_INITIALIZE;
  Defer ksCleanup ([&]() { ks_free (&ks); });
  for (const auto& c : cases) {
    INFO ("parsing " << c.qname);
    ks.l = 0;
    REQUIRE (kputs (c.sam.c_str(), &ks) >= 0);
    bam1_t* b = bam_init1();
    REQUIRE (b != nullptr);
    recs.push_back (b);
    REQUIRE (sam_parse1 (&ks, hdr, b) >= 0);
  }

  /* pile up and insert */
  sqlite3* conn = setup_db::init();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
  test::seed_file (conn);

  test::RecordFeed feed{.recs = &recs};
  bam_plp_t plp = bam_plp_init (test::next_fed_record, &feed);
  REQUIRE (plp != nullptr);
  Defer plpCleanup ([&]() { bam_plp_destroy (plp); });

  constexpr int64_t pileupPos0 = 99;
  const bam_pileup1_t* br_plpArr = nullptr;
  int plpTid = -1;
  int nPlp = -1;
  int64_t plpPos = -1;
  bool found = false;
  while ((br_plpArr = bam_plp64_auto (plp, &plpTid, &plpPos, &nPlp)) !=
         nullptr) {
    if (plpPos < pileupPos0) {
      continue;
    }
    REQUIRE (plpPos == pileupPos0);
    REQUIRE (static_cast<size_t> (nPlp) == cases.size());
    const auto status = hts2sql::insert_pileup (
        conn, br_plpArr, static_cast<size_t> (nPlp), "chr1",
        test::k_testFileId,
        [hdr] (int tid) { return sam_hdr_tid2name (hdr, tid); }
    );
    REQUIRE (status.code == hts2sql::InsertPileupStatus::success);
    found = true;
    break;
  }
  REQUIRE (found);

  /* check stored rows */
  sqlite3_stmt* o_stmt = nullptr;
  REQUIRE (
      sqlite3_prepare_v2 (
          conn, "SELECT * FROM reads;", -1, &o_stmt, NULL
      ) == SQLITE_OK
  );
  Defer stmtCleanup ([&]() { sqlite3_finalize (o_stmt); });

  using Col = schema::UserReadViewSelect;
  const auto isNull = [&] (int col) {
    return sqlite3_column_type (o_stmt, col) == SQLITE_NULL;
  };
  const auto asInt = [&] (int col) {
    return sqlite3_column_int64 (o_stmt, col);
  };
  const auto asText = [&] (int col) -> std::optional<std::string> {
    if (isNull (col)) {
      return std::nullopt;
    }
    return std::string (
        reinterpret_cast<const char*> (sqlite3_column_text (o_stmt, col)),
        static_cast<size_t> (sqlite3_column_bytes (o_stmt, col))
    );
  };

  size_t nRows = 0;
  while (sqlite3_step (o_stmt) == SQLITE_ROW) {
    ++nRows;
    const auto qname = asText (Col::qname);
    REQUIRE (qname.has_value());
    INFO ("read " << *qname);
    const auto it =
        std::find_if (cases.begin(), cases.end(), [&] (const Case& c) {
          return c.qname == *qname;
        });
    REQUIRE (it != cases.end());
    const auto& e = it->expect;

    // Expected and stored values are rendered in parallel, field by
    // field, and compared once per read.
    std::string want, got;
    const auto field = [&] (
                           std::string_view name, const auto& wantVal,
                           const auto& gotVal
                       ) {
      want += fmt::format (" {}={}", name, wantVal);
      got += fmt::format (" {}={}", name, gotVal);
    };
    const auto optText = [&] (int col) {
      return asText (col).value_or ("NULL");
    };
    const auto optInt = [&] (const auto& v) {
      return v ? std::to_string (*v) : std::string ("NULL");
    };
    const auto setOrNull = [&] (bool set) { return set ? "set" : "NULL"; };

    field ("start", e.start, asInt (Col::start));
    field ("end", e.end, asInt (Col::end));
    field ("cigar", e.cigar, optText (Col::cigar));
    field ("ncig", e.ncig, asInt (Col::ncig));
    field (
        "base", e.base ? std::string (1, *e.base) : std::string ("NULL"),
        optText (Col::base)
    );
    field (
        "basequal", optInt (e.basequal),
        isNull (Col::basequal) ? std::string ("NULL")
                               : std::to_string (asInt (Col::basequal))
    );
    field ("seq", setOrNull (e.hasSeq), setOrNull (!isNull (Col::seq)));
    field ("qual", setOrNull (e.hasQual), setOrNull (!isNull (Col::qual)));
    field ("mtid", e.mtid.value_or ("NULL"), optText (Col::mtid));
    field (
        "mstart", optInt (e.mstart),
        isNull (Col::mstart) ? std::string ("NULL")
                             : std::to_string (asInt (Col::mstart))
    );
    field (
        "tags", "NULL", optText (Col::tags)
    );  // no aux tags in any read
    // only where the case specifies a value
    if (e.qpos) {
      field ("qpos", *e.qpos, asInt (Col::qpos));
    }
    if (e.indel) {
      field ("indel", *e.indel, asInt (Col::indel));
    }
    if (e.isDel) {
      field ("is_del", *e.isDel, asInt (Col::is_del));
    }
    if (e.isHead) {
      field ("is_head", *e.isHead, asInt (Col::is_head));
    }
    if (e.isTail) {
      field ("is_tail", *e.isTail, asInt (Col::is_tail));
    }
    if (e.isRefSkip) {
      field ("is_refskip", *e.isRefSkip, asInt (Col::is_refskip));
    }
    CHECK (got == want);
  }
  CHECK (nRows == cases.size());
}

/* ---- TESTS dump_to_disk / load_from_disk ---- */

namespace test {

std::filesystem::path temp_db_path (std::string_view name)
{
  return std::filesystem::temp_directory_path() / name;
}

// Every column of every row of `sql`'s result, as one string.
std::string digest (sqlite3* conn, const char* sql)
{
  sqlite3_stmt* o_stmt = nullptr;
  REQUIRE (sqlite3_prepare_v2 (conn, sql, -1, &o_stmt, NULL) == SQLITE_OK);
  Defer stmtCleanup ([&]() { sqlite3_finalize (o_stmt); });
  std::string out;
  while (sqlite3_step (o_stmt) == SQLITE_ROW) {
    for (int i = 0; i < sqlite3_column_count (o_stmt); ++i) {
      if (sqlite3_column_type (o_stmt, i) == SQLITE_NULL) {
        out += "NULL";
      }
      else if (sqlite3_column_type (o_stmt, i) == SQLITE_BLOB) {
        const auto* br_blob =
            static_cast<const uint8_t*> (sqlite3_column_blob (o_stmt, i));
        for (int j = 0; j < sqlite3_column_bytes (o_stmt, i); ++j) {
          out += fmt::format ("{:02x}", br_blob[j]);
        }
      }
      else {
        out += reinterpret_cast<const char*> (
            sqlite3_column_text (o_stmt, i)
        );
      }
      out += '|';
    }
    out += '\n';
  }
  return out;
}

}  // namespace test

TEST_CASE ("dump_to_disk then load_from_disk round-trips a database")
{
  const auto path = test::temp_db_path ("apb-test-roundtrip.db");
  std::filesystem::remove (path);
  Defer removeFile ([&]() { std::filesystem::remove (path); });

  sqlite3* conn = setup_db::init();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
  test::seed_reads (conn, 2, std::make_optional<std::string> ("ACGTACGT"));
  REQUIRE (
      sqlite3_exec (
          conn,
          "INSERT INTO alignment_files (path, has_contig, has_reads) "
          "VALUES ('other.bam', 0, 0);",
          nullptr, nullptr, nullptr
      ) == SQLITE_OK
  );

  REQUIRE (
      query::dump_to_disk (conn, path.string()).code ==
      query::DiskDumpStatus::success
  );
  auto loadRet = setup_db::load_from_disk (path.string());
  REQUIRE (loadRet);
  sqlite3* loaded = *loadRet;
  Defer closeLoaded ([&]() { sqlite3_close_v2 (loaded); });

  CHECK (
      test::digest (loaded, "SELECT * FROM reads ORDER BY id;") ==
      test::digest (conn, "SELECT * FROM reads ORDER BY id;")
  );
  CHECK (
      test::digest (
          loaded, "SELECT * FROM alignment_files ORDER BY id;"
      ) ==
      test::digest (conn, "SELECT * FROM alignment_files ORDER BY id;")
  );
  CHECK (
      test::digest (loaded, "SELECT * FROM locus_metadata;") ==
      test::digest (conn, "SELECT * FROM locus_metadata;")
  );

  const auto expected = query::get_locus_data (conn);
  const auto got = query::get_locus_data (loaded);
  CHECK (got.contig == expected.contig);
  CHECK (got.pos == expected.pos);
  CHECK (got.start == expected.start);
  CHECK (got.end == expected.end);
  CHECK (got.refSlice == expected.refSlice);
  CHECK (got.nAln == expected.nAln);
}

TEST_CASE ("load_from_disk reports a typed error for unusable files")
{
  const auto path = test::temp_db_path ("apb-test-unusable.db");
  std::filesystem::remove (path);
  Defer removeFile ([&]() { std::filesystem::remove (path); });

  SUBCASE ("nonexistent file")
  {
    const auto ret = setup_db::load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().code == setup_db::LoadErr::openFail);
  }

  SUBCASE ("sqlite file with a different schema")
  {
    sqlite3* other = nullptr;
    REQUIRE (
        sqlite3_open_v2 (
            path.c_str(), &other,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL
        ) == SQLITE_OK
    );
    REQUIRE (
        sqlite3_exec (other, "CREATE TABLE x (a);", NULL, NULL, NULL) ==
        SQLITE_OK
    );
    sqlite3_close_v2 (other);

    const auto ret = setup_db::load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().code == setup_db::LoadErr::schemaMismatch);
  }

  SUBCASE ("apb database with no locus metadata")
  {
    sqlite3* conn = setup_db::init();
    Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
    REQUIRE (
        query::dump_to_disk (conn, path.string()).code ==
        query::DiskDumpStatus::success
    );

    const auto ret = setup_db::load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().code == setup_db::LoadErr::contentCorrupt);
  }
}

#endif  // DOCTEST_CONFIG_DISABLE
