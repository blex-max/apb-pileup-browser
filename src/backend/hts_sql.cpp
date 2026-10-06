#include "backend/hts_sql.hpp"

#include <fmt/format.h>
#include <htslib/sam.h>
#include <sqlite3.h>

#include <cstdint>
#include <cstdio>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "backend/hts_types.hpp"
#include "backend/schema.hpp"
#include "shared/apb_assert.hpp"
#include "shared/cleanup.hpp"

// Error handling convention for sqlite3 calls in this file:
//
// - APB_ASSERT/APB_UNREACHABLE where statements apb builds itself, run
//   against a schema apb created or has already verified (schema
//   fingerprint match, or a fresh PileupDB::init()). Any prepare/bind
//   failure, or a SQLITE_CONSTRAINT step failure, means apb's own
//   SQL/schema/bind order/invariants are corrupt and therefore a bug.
//   These states should not be reachable with valid input. Some invariants
//   are validated at multiple points - after the first validation, they
//   are considered unreachable
// - Returned/propagated error where failures are not within the control
//   of apb - i.e. user input/ouput, out of memory, etc.

PileupDB PileupDB::init()
{
  // Nothing herein should fail unless miswritten, hence use of UNREACHABLE
  PileupDB db;

  if (const auto rc = sqlite3_open (":memory:", &db.o_conn);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to open in-memory database: {}", sqlite3_errstr (rc)
        )
    );
  }

  auto sqlite_exec = [&db] (const std::string_view stmt) -> int {
    return sqlite3_exec (db, std::string{stmt}.c_str(), NULL, NULL, NULL);
  };

  if (const auto rc = sqlite_exec (schema::sqlSetTempStoreMemory);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to set temp_store pragma: {}", sqlite3_errmsg (db)
        )
    );
  }

  if (const auto rc = sqlite_exec (schema::sqlSetFK); rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to set foreign_key pragma: {}", sqlite3_errmsg (db)
        )
    );
  }

  if (const auto rc = sqlite_exec (schema::sqlCreateLocusTable);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to create metadata table: {}", sqlite3_errmsg (db)
        )
    );
  }

  if (const auto rc = sqlite_exec (schema::sqlCreateFileTable);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to create file table: {}", sqlite3_errmsg (db)
        )
    );
  }

  if (const auto rc = sqlite_exec (schema::sqlCreatePerRecordTable);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to create per record table: {}", sqlite3_errmsg (db)
        )
    );
  }

  if (const auto rc =
          sqlite_exec (schema::sqlCreatePerRecordInsertTrigger);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to create read insert trigger: {}", sqlite3_errmsg (db)
        )
    );
  }

  if (const auto rc = sqlite_exec (schema::sqlCreateUserView);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to create reads view: {}", sqlite3_errmsg (db)
        )
    );
  }

  return db;
}


// returns a fingerprint of the schema of the input database,
static std::string schema_fingerprint (sqlite3* db)
{
  sqlite3_stmt* o_stmt = NULL;
  if (const auto rc = sqlite3_prepare_v2 (
          db,
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

PileupDB::LoadStatus PileupDB::load_from_disk (
    PileupDB& db, std::string_view path
)
{
  /*
    Copy a database file on disk into an in-memory PileupDB,
    via sqlite3's online backup API.
  */
  APB_ASSERT (db.o_conn != nullptr);

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
    // that failed to open), not db.
    const std::string errMsg = sqlite3_errmsg (o_fileDb);
    return {
        .code = PileupDB::LoadStatus::openFail, .rc = sqlRc, .msg = errMsg
    };
  }

  if (o_backup = sqlite3_backup_init (db, "main", o_fileDb, "main");
      o_backup == NULL) {
    sqlRc = sqlite3_errcode (db);
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
            db, "PRAGMA quick_check;", -1, &o_stmt, NULL
        );
        rc != SQLITE_OK) {
      return {
          .code = LoadStatus::contentCorrupt,
          .rc = std::nullopt,
          .msg = fmt::format (
              "Could not verify database content: {}", sqlite3_errmsg (db)
          )
      };
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
          "Database corruption check aborted: {}\n", sqlite3_errmsg (db)
      );
      return {
          .code = LoadStatus::contentCorrupt,
          .rc = std::nullopt,
          .msg = problems
      };
    }
  }
  {
    // Verify the loaded schema matches apb's expected schema.
    const auto refdb{PileupDB::init()};

    auto refSchema = schema_fingerprint (refdb);
    auto loadedSchema = schema_fingerprint (db);
    if (refSchema != loadedSchema) {
      return {
          .code = LoadStatus::schemaMismatch,
          .rc = std::nullopt,
          .msg = std::nullopt
      };
    }
  }

  {
    // Verify locus metadata exists and is well-formed - the one thing
    // schema matching and quick_check don't catch.
    sqlite3_stmt* o_stmt = NULL;
    if (const auto rc = sqlite3_prepare_v2 (
            db, schema::MetaTableSelect::sql.data(),
            static_cast<int> (schema::MetaTableSelect::sql.size()),
            &o_stmt, NULL
        );
        rc != SQLITE_OK) {
      APB_UNREACHABLE (
          fmt::format (
              "failed to prepare locus metadata query: {}",
              sqlite3_errmsg (db)
          )
      );
    }
    Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

    const auto rc = sqlite3_step (o_stmt);
    if (rc == SQLITE_DONE) {
      return {
          .code = LoadStatus::contentCorrupt,
          .rc = std::nullopt,
          .msg =
              "database has no locus metadata - is this a valid "
              "apb dump?"
      };
    }
    if (rc != SQLITE_ROW) {
      APB_UNREACHABLE (
          fmt::format (
              "failed to read locus metadata row: {}", sqlite3_errmsg (db)
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
    if (!meta.valid()) {
      return {
          .code = LoadStatus::contentCorrupt,
          .rc = std::nullopt,
          .msg = "locus metadata in database is invalid or corrupt."
      };
    }
  }

  return {.code = LoadStatus::success};

err_sql: {
  sqlite3_backup_finish (o_backup);
  o_backup = NULL;
  // NOTE: per sqlite3 docs, errors from backup_init/backup_step
  // are stored on the destination handle, so db (the in-memory
  // connection being loaded into) is the right handle to query
  // here in every failure case above.
  const std::string errMsg = sqlite3_errmsg (db);
  return {.code = LoadStatus::copyFail, .rc = sqlRc, .msg = errMsg};
}
}


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
    const PileupDB& db, std::string_view prefix,
    const DynamicFragments& frags
)
{
  APB_ASSERT (prefix.back() != ';');

  sqlite3_stmt* stmt;

  std::string rsql_builtStmt{prefix};

  // build WHERE
  if (!frags.where.empty()) {
    rsql_builtStmt.append (" WHERE ");
    rsql_builtStmt.append (build_where_clause (frags.where));
  }

  if (!frags.orderBy.empty()) {
    rsql_builtStmt.append (" ORDER BY ");
    rsql_builtStmt.append (frags.orderBy);
  }

  rsql_builtStmt.append (";");  // end stmt

  // If these fail, then the user statement is not valid,
  // hence they are not unreachable
  int rc;
  if (rc = sqlite3_prepare_v2 (
          db, rsql_builtStmt.c_str(),
          static_cast<int> (rsql_builtStmt.size()), &stmt, NULL
      );
      rc != SQLITE_OK) {
    return std::unexpected (rc);
  }

  if (sqlite3_stmt_readonly (stmt) == 0) {
    return std::unexpected (SQLITE_READONLY);
  }

  return stmt;
}

PileupMetadata get_locus_data (const PileupDB& db)
{
  sqlite3_stmt* o_stmt = NULL;
  if (const auto rc = sqlite3_prepare_v2 (
          db, schema::MetaTableSelect::sql.data(),
          static_cast<int> (schema::MetaTableSelect::sql.size()), &o_stmt,
          NULL
      );
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to prepare locus metadata query: {}",
            sqlite3_errmsg (db)
        )
    );
  }
  Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

  if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_ROW) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to read locus metadata row: {}", sqlite3_errmsg (db)
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

  if (!out.valid()) {
    APB_UNREACHABLE ("locus metadata invalid post-validation");
  }

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

std::expected<uint32_t, int> count_rows (sqlite3_stmt* stmt)
{
  uint32_t nRow = 0;
  for (;; ++nRow) {
    const auto iterStatus = next_read (stmt);
    if (!iterStatus) {
      return std::unexpected (iterStatus.error());
    }
    if (*iterStatus == RowIterStatus::exhausted) {
      return nRow;
    }
  }
}

DiskDumpStatus dump_to_disk (const PileupDB& db, std::string_view path)
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

  if (o_backupConn = sqlite3_backup_init (o_dumpConn, "main", db, "main");
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
  pf.mStart = br_b1->core.mpos;  // <0 == unaligned (or no mate)

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
  pf.mtidName = mTidName != NULL ? mTidName : "";
  pf.rawCig = {br_cig, br_cig + nCig};
  pf.nCig = br_b1->core.n_cigar;

  {
    /* stringify seq, qual */
    // ASSUMPTION: seq and qual data present.
    const auto lq = static_cast<size_t> (br_b1->core.l_qseq);
    pf.seqBases.resize (lq);
    pf.qualAscii.resize (lq);

    const uint8_t* br_qs = bam_get_seq (br_b1);
    const uint8_t* br_qq = bam_get_qual (br_b1);
    for (size_t j = 0; j < lq; ++j) {
      pf.seqBases[j] = seq_nt16_str[bam_seqi (br_qs, j)];
      pf.qualAscii[j] = static_cast<char> (br_qq[j] + 33);
    }
    if (!br_p1->is_del) {
      pf.baseQual = br_qq[br_p1->qpos];
      pf.base = pf.seqBases[static_cast<size_t> (br_p1->qpos)];
    }
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

// FIXME: hoist out reference fetch, and take callback
// to get mtid rather than using hdr directly.
// Minimises error space, and makes reuse in demo.cpp easier
std::expected<void, InsertPileupErr> insert_pileup (
    PileupDB& db, const PileupIterator& pileupIter,
    const std::string& contigName, const sam_hdr_t* br_alnHdr,
    sqlite3_int64 aln_id, const std::optional<faidx_t*>& ff
)
{
  APB_ASSERT (pileupIter.span.valid(), pileupIter.span);
  APB_ASSERT (pileupIter.pos >= 0, pileupIter.pos);
  APB_ASSERT (pileupIter.tid >= 0, pileupIter.tid);
  APB_ASSERT (pileupIter.nPlp > 0, pileupIter.nPlp);
  APB_ASSERT (!contigName.empty());

  std::optional<std::string> refSlice;
  if (ff) {
    hts_pos_t regLen;
    auto* o_fetch = faidx_fetch_seq64 (
        *ff, contigName.c_str(), pileupIter.span.start,
        pileupIter.span.end - 1, &regLen
    );
    if (o_fetch == NULL) {
      return std::unexpected (
          InsertPileupErr{
              .code = InsertPileupErr::refFetchFail, .rc = regLen
          }
      );
    }
    refSlice = {o_fetch, static_cast<size_t> (regLen)};
    free (o_fetch);
  }

  auto rcInsMeta = insert_metadata (
      db, contigName, pileupIter.pos, pileupIter.span, refSlice
  );
  if (rcInsMeta != SQLITE_OK) {
    return std::unexpected (
        InsertPileupErr{.code = InsertPileupErr::sqlFail, .rc = rcInsMeta}
    );
  }

  auto* stmt = prepare_insert_reads_stmt (db);

  // manually begin/commit transaction.
  if (const auto rc = sqlite3_exec (db, "BEGIN;", NULL, NULL, NULL);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to begin transaction: {}", sqlite3_errmsg (db)
        )
    );
  }

  for (size_t i = 0; i < pileupIter.nPlp; ++i) {
    PileupFields readI;
    const auto* p1 = &pileupIter.br_plpArr[i];

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
                       : sam_hdr_tid2name (br_alnHdr, b1->core.mtid);
      }
      else {
        mtidName = NULL;
      }
    }

    if (!fill_fields (readI, p1, mtidName)) {
      return std::unexpected (
          InsertPileupErr{
              .code = InsertPileupErr::auxParseFail,
              .rc = std::nullopt /* no specific code */
          }
      );
    }

    bind_read_data (stmt, readI, aln_id);

    if (const auto rc = sqlite3_step (stmt); rc != SQLITE_DONE) {
      if ((rc & 0xFF) == SQLITE_CONSTRAINT) {
        APB_UNREACHABLE (
            fmt::format (
                "read insert violated a schema constraint: {}",
                sqlite3_errmsg (db)
            )
        );
      }
      return std::unexpected (
          InsertPileupErr{.code = InsertPileupErr::sqlFail, .rc = rc}
      );
    }
    sqlite3_reset (stmt);  // rc mirrors the step already checked above
    sqlite3_clear_bindings (stmt);  // cannot fail per sqlite3 docs
  }

  if (const auto rc = sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to commit transaction: {}", sqlite3_errmsg (db)
        )
    );
  }
  return {};
};

[[nodiscard]] int insert_metadata (
    PileupDB& db, const std::string& contigName, int64_t pileupPos,
    const GenomicSpan& pileupSpan,
    const std::optional<std::string>& refSlice
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

  sqlite3_stmt* stmt;
  if (const auto rc = sqlite3_prepare_v2 (
          db, schema::sqlInsertMetadata.data(),
          static_cast<int> (schema::sqlInsertMetadata.size()), &stmt, NULL
      );
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to prepare metadata insert statement: {}",
            sqlite3_errmsg (db)
        )
    );
  }
  Defer finalise_stmt ([&stmt]() { sqlite3_finalize (stmt); });

  // NOTE: TIED TO SCHEMA ORDER. BE CAREFUL!
  int col = 1;
  auto bindOK = [&] (int rc) {
    if (rc != SQLITE_OK) {
      APB_UNREACHABLE (
          fmt::format ("bind failed: {}", sqlite3_errstr (rc))
      );
    }
  };

  bindOK (
      sqlite3_bind_text (stmt, col++, APB_VERSION, -1, SQLITE_TRANSIENT)
  );
  bindOK (sqlite3_bind_text (
      stmt, col++, contigName.c_str(), -1, SQLITE_TRANSIENT
  ));
  bindOK (sqlite3_bind_int64 (stmt, col++, pileupPos + 1));
  bindOK (sqlite3_bind_int64 (stmt, col++, pileupSpan.start + 1));
  bindOK (sqlite3_bind_int64 (stmt, col++, pileupSpan.end));
  if (!refSlice) {
    bindOK (sqlite3_bind_null (stmt, col++));
  }
  else {
    APB_ASSERT (!(*refSlice).empty());
    bindOK (sqlite3_bind_text (
        stmt, col++, (*refSlice).c_str(),
        static_cast<int> ((*refSlice).size()), SQLITE_TRANSIENT
    ));
  }

  if (const auto rc = sqlite3_step (stmt); rc != SQLITE_DONE) {
    if ((rc & 0xFF) == SQLITE_CONSTRAINT) {
      APB_UNREACHABLE (
          fmt::format (
              "metadata insert violated a schema constraint: {}",
              sqlite3_errmsg (db)
          )
      );
    }
    return rc;
  }
  return 0;
}

sqlite3_stmt* prepare_insert_reads_stmt (PileupDB& db)
{
  sqlite3_stmt* stmt;
  if (const auto rc = sqlite3_prepare_v2 (
          db, schema::sqlInsertReads.data(),
          static_cast<int> (schema::sqlInsertReads.size()), &stmt, NULL
      );
      rc != SQLITE_OK) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to prepare insert statement for per-record data "
            "table: {}",
            sqlite3_errmsg (db)
        )
    );
  }
  return stmt;
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
    bindOK (sqlite3_bind_int (stmt, col++, *pf.baseQual));
  }
  else {
    bindOK (sqlite3_bind_null (stmt, col++));
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
  bindOK (sqlite3_bind_text (
      stmt, col++, pf.seqBases.data(),
      static_cast<int> (pf.seqBases.size()), SQLITE_TRANSIENT
  ));
  bindOK (sqlite3_bind_text (
      stmt, col++, pf.qualAscii.data(),
      static_cast<int> (pf.qualAscii.size()), SQLITE_TRANSIENT
  ));
  if (!pf.mtidName.empty()) {
    bindOK (sqlite3_bind_text (
        stmt, col++, pf.mtidName.data(),
        static_cast<int> (pf.mtidName.size()), SQLITE_TRANSIENT
    ));
  }
  else {
    bindOK (sqlite3_bind_null (stmt, col++));
  }
  if (pf.mStart < 0) {
    bindOK (sqlite3_bind_null (stmt, col++));
  }
  else {
    bindOK (sqlite3_bind_int64 (stmt, col++, pf.mStart + 1));
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

/* ---- TESTS stringify_cigar ---- */

TEST_CASE ("stringify_cigar renders CIGAR arrays as text")
{
  auto gen = [] (uint32_t len, uint32_t op) {
    return bam_cigar_gen (len, op);
  };

  SUBCASE ("single op")
  {
    std::vector<uint32_t> cig{gen (50, BAM_CMATCH)};
    CHECK (hts2sql::stringify_cigar (cig.data(), cig.size()) == "50M");
  }

  SUBCASE ("multi op")
  {
    std::vector<uint32_t> cig{
        gen (10, BAM_CSOFT_CLIP), gen (40, BAM_CMATCH), gen (5, BAM_CINS),
        gen (2, BAM_CDEL)
    };
    CHECK (
        hts2sql::stringify_cigar (cig.data(), cig.size()) == "10S40M5I2D"
    );
  }

  SUBCASE ("zero ops")
  {
    CHECK (hts2sql::stringify_cigar (nullptr, 0) == "");
  }
}

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
TEST_CASE ("aux1_to_json converts scalar/char/string tags")
{
  SUBCASE ("numeric scalar (type 'c', int8)")
  {
    uint8_t buf[] = {'N', 'M', 'c', 5};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"NM\":5");
  }

  SUBCASE ("char (type 'A')")
  {
    uint8_t buf[] = {'X', 'A', 'A', 'Q'};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XA\":\"Q\"");
  }

  SUBCASE ("string (type 'Z')")
  {
    uint8_t buf[] = {'R', 'G', 'Z', 's', 'a', 'm',
                     'p', 'l', 'e', '1', '\0'};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"RG\":\"sample1\"");
  }

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

namespace test {

hts2sql::PileupFields make_basic_fields (hts_pos_t mStart = -1)
{
  hts2sql::PileupFields pf{};
  pf.qName = "read0";
  pf.flag = 99;
  pf.start = 99;  // 0-indexed input -> db start == 100
  pf.end = 170;  // unshifted -> db end == 170
  pf.mapQ = 60;
  pf.base = 'A';
  pf.baseQual = 37;
  pf.qPos = 4;  // 0-indexed input -> db qpos == 5
  pf.indel = 0;
  pf.isDel = false;
  pf.isHead = false;
  pf.isTail = false;
  pf.isRefSkip = false;
  pf.cig = "50M";
  pf.seqBases = "ACGTACGTAC";
  pf.qualAscii = "IIIIIIIIII";
  pf.mtidName = "";
  pf.mStart = mStart;
  pf.auxJson = "";
  pf.rawCig = {static_cast<uint32_t> (bam_cigar_gen (50, BAM_CMATCH))};
  pf.nCig = pf.rawCig.size();
  return pf;
}


// alignment_files row that per-record inserts reference via path_id.
constexpr sqlite3_int64 k_testFileId = 1;

void seed_file (PileupDB& db)
{
  const char* sql =
      "INSERT INTO alignment_files (id, path) VALUES (1, 'test.bam');";
  REQUIRE (sqlite3_exec (db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
}

}  // namespace test

TEST_CASE (
    "insert_metadata converts 0-indexed input to 1-indexed locus "
    "data"
)
{
  PileupDB db = PileupDB::init();

  SUBCASE ("with a reference slice")
  {
    REQUIRE (
        hts2sql::insert_metadata (
            db, "chr1", 99, GenomicSpan{.start = 99, .end = 200},
            std::make_optional<std::string> ("ACGTACGT")
        ) == SQLITE_OK
    );

    auto locus = query::get_locus_data (db);
    CHECK (locus.contig == "chr1");
    CHECK (locus.pos == 100);
    CHECK (locus.start == 100);
    CHECK (locus.end == 200);
    REQUIRE (locus.refSlice.has_value());
    CHECK (*locus.refSlice == "ACGTACGT");
  }

  SUBCASE ("with no reference slice")
  {
    REQUIRE (
        hts2sql::insert_metadata (
            db, "chr2", 49, GenomicSpan{.start = 39, .end = 60},
            std::nullopt
        ) == SQLITE_OK
    );

    auto locus = query::get_locus_data (db);
    CHECK (locus.contig == "chr2");
    CHECK (locus.pos == 50);
    CHECK (locus.start == 40);
    CHECK (locus.end == 60);
    CHECK_FALSE (locus.refSlice.has_value());
  }
}

TEST_CASE (
    "bind_pileup_fields converts 0-indexed input to 1-indexed row "
    "data"
)
{
  PileupDB db = PileupDB::init();
  test::seed_file (db);

  SUBCASE ("mStart >= 0 shifts to a 1-indexed mstart")
  {
    auto stmt = hts2sql::prepare_insert_reads_stmt (db);
    auto pf = test::make_basic_fields (49);
    hts2sql::bind_read_data (stmt, pf, test::k_testFileId);
    REQUIRE (sqlite3_step (stmt) == SQLITE_DONE);

    sqlite3_stmt* o_stmt = NULL;
    const std::string_view sql = "SELECT * FROM reads;";
    REQUIRE (
        sqlite3_prepare_v2 (
            db, sql.data(), static_cast<int> (sql.size()), &o_stmt, NULL
        ) == SQLITE_OK
    );
    REQUIRE (sqlite3_step (o_stmt) == SQLITE_ROW);

    CHECK (
        sqlite3_column_int64 (o_stmt, schema::UserReadViewSelect::start) ==
        pf.start + 1
    );
    CHECK (
        sqlite3_column_int64 (o_stmt, schema::UserReadViewSelect::end) ==
        pf.end
    );
    CHECK (
        sqlite3_column_int64 (o_stmt, schema::UserReadViewSelect::qpos) ==
        pf.qPos + 1
    );
    CHECK (
        sqlite3_column_int64 (
            o_stmt, schema::UserReadViewSelect::mstart
        ) == pf.mStart + 1
    );
    sqlite3_finalize (o_stmt);
  }

  SUBCASE ("mStart < 0 stays NULL rather than shifting")
  {
    auto stmt = hts2sql::prepare_insert_reads_stmt (db);
    auto pf = test::make_basic_fields (-1);
    hts2sql::bind_read_data (stmt, pf, test::k_testFileId);
    REQUIRE (sqlite3_step (stmt) == SQLITE_DONE);

    sqlite3_stmt* o_stmt = NULL;
    const std::string_view sql = "SELECT * FROM reads;";
    REQUIRE (
        sqlite3_prepare_v2 (
            db, sql.data(), static_cast<int> (sql.size()), &o_stmt, NULL
        ) == SQLITE_OK
    );
    REQUIRE (sqlite3_step (o_stmt) == SQLITE_ROW);

    CHECK (
        sqlite3_column_type (o_stmt, schema::UserReadViewSelect::mstart) ==
        SQLITE_NULL
    );
    sqlite3_finalize (o_stmt);
  }
}

TEST_CASE (
    "CHECK(json_valid(tags)) accepts valid JSON and rejects "
    "malformed JSON"
)
{
  PileupDB db = PileupDB::init();
  test::seed_file (db);

  SUBCASE ("valid JSON is accepted")
  {
    auto stmt = hts2sql::prepare_insert_reads_stmt (db);
    auto pf = test::make_basic_fields();
    pf.auxJson = "{\"XY\":[1,2,3]}";
    hts2sql::bind_read_data (stmt, pf, test::k_testFileId);
    CHECK (sqlite3_step (stmt) == SQLITE_DONE);
  }

  SUBCASE (
      "deliberately invalid JSON is rejected by the CHECK "
      "constraint"
  )
  {
    auto stmt = hts2sql::prepare_insert_reads_stmt (db);
    auto pf = test::make_basic_fields();
    pf.auxJson = "{\"XY\":[1,2,3,]}";  // trailing comma -- malformed
    hts2sql::bind_read_data (stmt, pf, test::k_testFileId);
    const int rc = sqlite3_step (stmt);
    CHECK ((rc & 0xFF) == SQLITE_CONSTRAINT);
  }
}

/* ---- TESTS build_where_clause / prepare_select_reads ---- */

namespace test {

struct SeededDb {
  PileupDB db;
};

SeededDb make_seeded_db()
{
  PileupDB db = PileupDB::init();
  seed_file (db);
  REQUIRE (
      hts2sql::insert_metadata (
          db, "chr1", 149, GenomicSpan{.start = 99, .end = 200},
          std::nullopt
      ) == SQLITE_OK
  );

  auto stmt = hts2sql::prepare_insert_reads_stmt (db);

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
    pf.seqBases = "A";
    pf.qualAscii = "I";
    pf.mtidName = "";
    pf.mStart = -1;
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

  return SeededDb{.db = std::move (db)};
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

TEST_CASE ("prepare_select_reads with no fragments returns every row")
{
  auto seeded = test::make_seeded_db();

  auto stmtRet = query::prepare_select_reads (
      seeded.db, schema::UserReadViewSelect::sqlPrefix, {}
  );
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};
  auto countRet = query::count_rows (stmt);
  REQUIRE (countRet);
  CHECK (*countRet == 3);
}

TEST_CASE ("prepare_select_reads filters on WHERE fragments")
{
  auto seeded = test::make_seeded_db();

  SUBCASE ("single fragment")
  {
    query::DynamicFragments frags{.where = {"flag = 99"}, .orderBy = ""};
    auto stmtRet = query::prepare_select_reads (
        seeded.db, schema::UserReadViewSelect::sqlPrefix, frags
    );
    REQUIRE (stmtRet);
    auto stmt{std::move (*stmtRet)};
    auto countRet = query::count_rows (stmt);
    REQUIRE (countRet);
    CHECK (*countRet == 2);  // readA, readC
  }

  SUBCASE ("multiple fragments")
  {
    query::DynamicFragments frags{
        .where = {"flag = 99", "AND mapq > 50"}, .orderBy = ""
    };
    auto stmtRet = query::prepare_select_reads (
        seeded.db, schema::UserReadViewSelect::sqlPrefix, frags
    );
    REQUIRE (stmtRet);
    auto stmt{std::move (*stmtRet)};
    auto countRet = query::count_rows (stmt);
    REQUIRE (countRet);
    CHECK (*countRet == 1);  // readA only (flag 99, mapq 60)
  }
}

TEST_CASE ("prepare_select_reads honours ORDER BY")
{
  auto seeded = test::make_seeded_db();

  query::DynamicFragments frags{.where = {}, .orderBy = "start DESC"};
  auto stmtRet = query::prepare_select_reads (
      seeded.db, schema::UserReadViewSelect::sqlPrefix, frags
  );
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};

  auto r1 = query::next_read (stmt);
  REQUIRE (r1);
  REQUIRE (*r1 == query::RowIterStatus::rowAvail);
  CHECK (
      sqlite3_column_int64 (stmt, schema::UserReadViewSelect::start) == 140
  );  // readC

  auto r2 = query::next_read (stmt);
  REQUIRE (r2);
  REQUIRE (*r2 == query::RowIterStatus::rowAvail);
  CHECK (
      sqlite3_column_int64 (stmt, schema::UserReadViewSelect::start) == 120
  );  // readB

  auto r3 = query::next_read (stmt);
  REQUIRE (r3);
  REQUIRE (*r3 == query::RowIterStatus::rowAvail);
  CHECK (
      sqlite3_column_int64 (stmt, schema::UserReadViewSelect::start) == 100
  );  // readA
}

TEST_CASE (
    "prepare_select_reads surfaces an sqlite3 error on a malformed "
    "fragment"
)
{
  auto seeded = test::make_seeded_db();

  query::DynamicFragments frags{.where = {"flag ="}, .orderBy = ""};
  auto stmtRet = query::prepare_select_reads (
      seeded.db, schema::UserReadViewSelect::sqlPrefix, frags
  );
  REQUIRE_FALSE (stmtRet);
}

TEST_CASE (
    "prepare_select_reads rejects a non-SELECT prefix via the "
    "readonly guard"
)
{
  auto seeded = test::make_seeded_db();

  auto stmtRet = query::prepare_select_reads (
      seeded.db, "DELETE FROM per_record_data", {}
  );
  REQUIRE_FALSE (stmtRet);
  CHECK (stmtRet.error() == SQLITE_READONLY);
}

TEST_CASE (
    "a stacked-query WHERE fragment only compiles up to the first "
    "';' -- trailing SQL is inert, not executed"
)
{
  auto seeded = test::make_seeded_db();

  query::DynamicFragments frags{
      .where = {"1=1; DELETE FROM reads"}, .orderBy = ""
  };
  auto stmtRet = query::prepare_select_reads (
      seeded.db, schema::UserReadViewSelect::sqlPrefix, frags
  );
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};
  CHECK (sqlite3_stmt_readonly (stmt));
  auto countRet = query::count_rows (stmt);
  REQUIRE (countRet);
  CHECK (*countRet == 3);  // DELETE never ran

  // Confirm directly against the table too.
  sqlite3_stmt* o_check = NULL;
  const std::string_view sql = "SELECT COUNT(*) FROM reads;";
  REQUIRE (
      sqlite3_prepare_v2 (
          seeded.db, sql.data(), static_cast<int> (sql.size()), &o_check,
          NULL
      ) == SQLITE_OK
  );
  REQUIRE (sqlite3_step (o_check) == SQLITE_ROW);
  CHECK (sqlite3_column_int64 (o_check, 0) == 3);
  sqlite3_finalize (o_check);
}

#endif  // DOCTEST_CONFIG_DISABLE
