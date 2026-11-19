#include "apb_db_core.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "apb_db_schema.hpp"
#include "utility/apb_assert.hpp"
#include "utility/cleanup.hpp"

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
  TRY_OK_INT (exec_sql (conn, schema::sqlCreateFileTable));
  TRY_OK_INT (exec_sql (conn, schema::sqlCreatePerRecordTable));
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
    // Integrity check tests schema constraints
    // e.g. CHECK and STRICT
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

  return apb_validate_result_t{.code = apb_validate_result_t::success};
}

// insert pileup into db for a single alignment file.
// CONVERTS FROM 0-INDEXED HALF OPEN COORDINATES TO 1-INDEXED CLOSED COORDINATES
[[nodiscard]] int apb_insert_records (
    sqlite3* conn, std::span<const apb_input_record1_t> recs,
    sqlite3_int64 alnId
)
{
  // NOTE: does not validate inputs; relies on schema constraints.
  // FIXME: double check that.
  sqlite3_stmt* o_stmt;
  if (const auto rc = sqlite3_prepare_v2 (
          conn, schema::sqlInsertReads.data(),
          static_cast<int> (schema::sqlInsertReads.size()), &o_stmt, NULL
      );
      rc != SQLITE_OK) {
    return rc;
  }
  Defer stmt_cleanup ([&o_stmt]() { return sqlite3_finalize (o_stmt); });

  /* bind to statement */
  // CONVERTS FROM 0-INDEXED apb_pileup_t TO 1-INDEXED DB REPRESENTATION
  // INSERTION ORDER TIED TO SCHEMA; BE CAREFUL! (apb_db_schema.hpp)
  for (const auto& rec1 : recs) {
    int col = 1;
    TRY_OK_INT (sqlite3_bind_text (
        o_stmt, col++, rec1.qName.data(),
        static_cast<int> (rec1.qName.size()), SQLITE_TRANSIENT
    ));
    TRY_OK_INT (sqlite3_bind_int (o_stmt, col++, rec1.flag));
    TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col++, rec1.start_0ho + 1));
    TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col++, rec1.end_0ho));
    TRY_OK_INT (sqlite3_bind_int (o_stmt, col++, rec1.mapQ));

    if (rec1.base) {
      TRY_OK_INT (sqlite3_bind_text (
          o_stmt, col++, &(*rec1.base), 1, SQLITE_TRANSIENT
      ));
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmt, col++));
    }

    if (rec1.baseQual) {
      TRY_OK_INT (sqlite3_bind_int (o_stmt, col++, *rec1.baseQual));
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmt, col++));
    }

    TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col++, rec1.qPos_0ho + 1));
    TRY_OK_INT (sqlite3_bind_int (o_stmt, col++, rec1.indel));
    TRY_OK_INT (
        sqlite3_bind_int (o_stmt, col++, static_cast<int> (rec1.isDel))
    );
    TRY_OK_INT (
        sqlite3_bind_int (o_stmt, col++, static_cast<int> (rec1.isHead))
    );
    TRY_OK_INT (
        sqlite3_bind_int (o_stmt, col++, static_cast<int> (rec1.isTail))
    );
    TRY_OK_INT (
        sqlite3_bind_int (o_stmt, col++, static_cast<int> (rec1.isRefSkip))
    );
    TRY_OK_INT (sqlite3_bind_text (
        o_stmt, col++, rec1.cigStr.data(),
        static_cast<int> (rec1.cigStr.size()), SQLITE_TRANSIENT
    ));

    if (rec1.seq) {
      TRY_OK_INT (sqlite3_bind_text (
          o_stmt, col++, rec1.seq->data(),
          static_cast<int> (rec1.seq->size()), SQLITE_TRANSIENT
      ));
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmt, col++));
    }

    if (rec1.rawQual) {
      TRY_OK_INT (sqlite3_bind_blob (
          o_stmt, col++, rec1.rawQual->data(),
          static_cast<int> (rec1.rawQual->size()), SQLITE_TRANSIENT
      ));
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmt, col++));
    }

    if (rec1.mtidName) {
      TRY_OK_INT (sqlite3_bind_text (
          o_stmt, col++, rec1.mtidName->data(),
          static_cast<int> (rec1.mtidName->size()), SQLITE_TRANSIENT
      ));
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmt, col++));
    }

    if (rec1.mStart_0ho) {
      TRY_OK_INT (
          sqlite3_bind_int64 (o_stmt, col++, rec1.mStart_0ho.value() + 1)
      );
    }
    else {
      TRY_OK_INT (sqlite3_bind_null (o_stmt, col++));
    }

    if (rec1.auxJson.empty()) {
      TRY_OK_INT (sqlite3_bind_null (o_stmt, col++));
    }
    else {
      TRY_OK_INT (sqlite3_bind_text (
          o_stmt, col++, rec1.auxJson.data(),
          static_cast<int> (rec1.auxJson.size()), SQLITE_TRANSIENT
      ));
    }

    TRY_OK_INT (sqlite3_bind_blob (
        o_stmt, col++, rec1.cigOps.data(),
        static_cast<int> (rec1.cigOps.size() << 2), SQLITE_TRANSIENT
    ));
    TRY_OK_INT (sqlite3_bind_int (
        o_stmt, col++, static_cast<int> (rec1.cigOps.size())
    ));
    TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col++, alnId));

    if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_DONE) {
      return rc;
    }
    sqlite3_reset (o_stmt);  // rc mirrors the step already checked above
    sqlite3_clear_bindings (o_stmt);  // cannot fail per sqlite3 docs
  }

  return {};
}

[[nodiscard]] int apb_insert_locus (
    sqlite3* conn, const std::string& contigName, int64_t pos_0ho,
    int64_t spanStart_0ho, int64_t spanEnd_0ho,
    std::optional<std::string_view> ref
)
{
  /*
    insert the pileup locus into the database's single metadata row.
    CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
  */
  APB_ASSERT (conn != nullptr);
  APB_ASSERT (!contigName.empty());
  APB_ASSERT (
      spanStart_0ho >= 0 && spanEnd_0ho > spanStart_0ho, spanStart_0ho,
      spanEnd_0ho
  );
  APB_ASSERT (pos_0ho >= 0, pos_0ho);
  APB_ASSERT (pos_0ho >= spanStart_0ho, pos_0ho, spanStart_0ho);
  APB_ASSERT (
      pos_0ho < spanEnd_0ho, pos_0ho, spanEnd_0ho
  );  // end-exclusive
  if (ref) {
    APB_ASSERT (!ref->empty());
  }

  uint16_t nAln;
  {
    sqlite3_stmt* o_stmt;
    TRY_OK_INT (sqlite3_prepare_v2 (
        conn, "SELECT COUNT(*) FROM alignment_files;", -1, &o_stmt, NULL
    ));
    Defer count_cleanup ([&o_stmt]() { sqlite3_finalize (o_stmt); });
    if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_ROW) {
      return rc;
    }
    nAln = static_cast<uint16_t> (sqlite3_column_int (o_stmt, 0));
  }

  sqlite3_stmt* o_stmt;
  TRY_OK_INT (sqlite3_prepare_v2 (
      conn, schema::sqlInsertMetadata.data(),
      static_cast<int> (schema::sqlInsertMetadata.size()), &o_stmt, NULL
  ));
  Defer stmt_cleanup ([&o_stmt]() { sqlite3_finalize (o_stmt); });

  // NOTE: TIED TO SCHEMA ORDER. BE CAREFUL!
  int col = 1;

  TRY_OK_INT (sqlite3_bind_text (
      o_stmt, col++, contigName.c_str(), -1, SQLITE_TRANSIENT
  ));
  TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col++, pos_0ho + 1));
  TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col++, spanStart_0ho + 1));
  TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col++, spanEnd_0ho));
  if (!ref) {
    TRY_OK_INT (sqlite3_bind_null (o_stmt, col++));
  }
  else {
    TRY_OK_INT (sqlite3_bind_text (
        o_stmt, col++, ref->data(), static_cast<int> (ref->size()),
        SQLITE_TRANSIENT
    ));
  }
  TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col, nAln));

  if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_DONE) {
    return rc;
  }
  return {};
}

std::expected<int, int> apb_insert_aln (
    sqlite3* conn, std::string_view alnPath, bool hasContig
)
{
  APB_ASSERT (conn != nullptr);
  APB_ASSERT (!alnPath.empty());

  sqlite3_stmt* o_fileInsertStmt;
  Defer file_stmt_cleanup ([&o_fileInsertStmt]() {
    sqlite3_finalize (o_fileInsertStmt);
  });
  TRY_OK_EXP (sqlite3_prepare_v2 (
      conn, schema::sqlInsertFile.data(),
      static_cast<int> (schema::sqlInsertFile.size()), &o_fileInsertStmt,
      NULL
  ));
  TRY_OK_EXP (sqlite3_bind_text (
      o_fileInsertStmt, 1, alnPath.data(),
      static_cast<int> (alnPath.size()), SQLITE_TRANSIENT
  ));
  TRY_OK_EXP (
      sqlite3_bind_int (o_fileInsertStmt, 2, static_cast<bool> (hasContig))
  );
  if (const auto rc = sqlite3_step (o_fileInsertStmt); rc != SQLITE_DONE) {
    return std::unexpected (rc);
  }
  file_stmt_cleanup.invoke();
  return sqlite3_last_insert_rowid (conn);
}


/* --- TESTS --- */
#ifndef DOCTEST_CONFIG_DISABLE

#include <cstring>
#include <vector>

#include "doctest.h"
#include "fixtures.hpp"

TEST_CASE ("apb_insert_records stores records as 1-based rows")
{
  sqlite3* conn = test::new_db();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
  const auto alnId = apb_insert_aln (conn, "test.bam", true);
  REQUIRE (alnId);

  // packed as htslib: len << 4 | op (BAM_CMATCH = 0)
  constexpr uint32_t k_cig10M = 10U << 4;
  constexpr uint32_t k_cig6M = 6U << 4;

  // every optional set
  apb_input_record1_t full;
  full.qName = "full";
  full.flag = 99;
  full.start_0ho = 94;
  full.end_0ho = 104;
  full.mapQ = 60;
  full.qPos_0ho = 6;
  full.indel = 2;
  full.isHead = true;
  full.isRefSkip = true;
  full.seq = "ACGTTGCAAG";
  full.rawQual =
      std::vector<uint8_t>{32, 33, 34, 35, 36, 37, 38, 39, 40, 41};
  full.base = 'C';  // seq[6]
  full.baseQual = 38;  // qual[6]
  full.cigStr = "10M";
  full.cigOps = {k_cig10M};
  full.mtidName = "chr2";
  full.mStart_0ho = 499;
  full.auxJson = R"({"NM":1})";

  // every optional empty
  apb_input_record1_t empty;
  empty.qName = "empty";
  empty.flag = 0;
  empty.start_0ho = 99;
  empty.end_0ho = 105;
  empty.mapQ = 0;
  empty.qPos_0ho = 0;
  empty.indel = 0;
  empty.isDel = true;
  empty.isTail = true;
  empty.cigStr = "6M";
  empty.cigOps = {k_cig6M};

  const std::vector<apb_input_record1_t> recs{full, empty};
  REQUIRE (apb_insert_records (conn, recs, *alnId) == SQLITE_OK);

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
    CHECK (asText (Col::base) == "C");
    CHECK (asInt (Col::basequal) == 38);
    CHECK (asInt (Col::qpos) == 7);
    CHECK (asInt (Col::indel) == 2);
    CHECK (asInt (Col::is_del) == 0);
    CHECK (asInt (Col::is_head) == 1);
    CHECK (asInt (Col::is_tail) == 0);
    CHECK (asInt (Col::is_refskip) == 1);
    CHECK (asText (Col::cigar) == "10M");
    CHECK (asText (Col::seq) == "ACGTTGCAAG");
    CHECK (
        sqlite3_column_bytes (o_stmt, Col::qual) ==
        static_cast<int> (full.rawQual->size())
    );
    CHECK (
        std::memcmp (
            sqlite3_column_blob (o_stmt, Col::qual), full.rawQual->data(),
            full.rawQual->size()
        ) == 0
    );
    CHECK (asText (Col::mtid) == "chr2");
    CHECK (asInt (Col::mstart) == 500);
    CHECK (asText (Col::tags) == R"({"NM":1})");
    CHECK (asInt (Col::ncig) == 1);
    CHECK (asInt (Col::path_id) == *alnId);
  }

  SUBCASE ("empty")
  {
    select ("empty");
    CHECK (asInt (Col::start) == 100);
    CHECK (asInt (Col::end) == 105);
    CHECK (asInt (Col::qpos) == 1);
    CHECK (asInt (Col::is_del) == 1);
    CHECK (asInt (Col::is_tail) == 1);
    CHECK (asText (Col::cigar) == "6M");
    CHECK (isNull (Col::base));
    CHECK (isNull (Col::basequal));
    CHECK (isNull (Col::seq));
    CHECK (isNull (Col::qual));
    CHECK (isNull (Col::mtid));
    CHECK (isNull (Col::mstart));
    CHECK (isNull (Col::tags));
  }

  SUBCASE ("unknown alignment file fails on foreign key")
  {
    const auto rc = apb_insert_records (conn, recs, *alnId + 1000);
    CHECK ((rc & 0xFF) == SQLITE_CONSTRAINT);
  }
}

TEST_CASE ("apb_insert_aln returns ids, and an error for a duplicate path")
{
  sqlite3* conn = test::new_db();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });

  const auto first = apb_insert_aln (conn, "a.bam", true);
  REQUIRE (first);
  const auto second = apb_insert_aln (conn, "b.bam", false);
  REQUIRE (second);
  CHECK (*second != *first);

  // UNIQUE (path) violation must surface as an error, not an id
  const auto dup = apb_insert_aln (conn, "a.bam", true);
  REQUIRE_FALSE (dup);
  CHECK ((dup.error() & 0xFF) == SQLITE_CONSTRAINT);
}

#endif  // DOCTEST_CONFIG_DISABLE
