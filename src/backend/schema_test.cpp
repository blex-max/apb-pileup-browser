#include "backend/schema.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <string>

#include "backend/hts_sql.hpp"
#include "doctest.h"

// Bypasses hts2sql::insert_metadata/bind_pileup_fields entirely -- inserts
// via bound statements directly, so a CHECK/TRIGGER violation surfaces as
// a plain sqlite3_step rc instead of tripping APB_UNREACHABLE (which only
// wraps CONSTRAINT failures reached through the real insert path).

namespace {

int insert_raw_metadata (
    PileupDB& db, int64_t pos, int64_t start, int64_t end
)
{
  SqliteStmt stmt;
  REQUIRE (
      sqlite3_prepare_v2 (
          db, schema::sqlInsertMetadata.data(),
          static_cast<int> (schema::sqlInsertMetadata.size()),
          &stmt.o_stmt, NULL
      ) == SQLITE_OK
  );

  int col = 1;
  sqlite3_bind_text (stmt, col++, "test", -1, SQLITE_STATIC);
  sqlite3_bind_text (stmt, col++, "chr1", -1, SQLITE_STATIC);
  sqlite3_bind_int64 (stmt, col++, pos);
  sqlite3_bind_int64 (stmt, col++, start);
  sqlite3_bind_int64 (stmt, col++, end);
  sqlite3_bind_null (stmt, col++);

  return sqlite3_step (stmt);
}

// Fixed dummy values for every reads column except start/end, chosen to
// satisfy the reads table's own per-row CHECKs unconditionally -- so a
// CONSTRAINT failure here can only come from validate_read_span.
int insert_raw_read (PileupDB& db, int64_t start, int64_t end)
{
  SqliteStmt stmt;
  REQUIRE (
      sqlite3_prepare_v2 (
          db, schema::sqlInsertReads.data(),
          static_cast<int> (schema::sqlInsertReads.size()), &stmt.o_stmt,
          NULL
      ) == SQLITE_OK
  );

  const uint8_t cigBlob[4] = {0, 0, 0, 0};

  int col = 1;
  sqlite3_bind_text (stmt, col++, "dummy", -1, SQLITE_STATIC);
  sqlite3_bind_int (stmt, col++, 0);  // flag
  sqlite3_bind_int64 (stmt, col++, start);
  sqlite3_bind_int64 (stmt, col++, end);
  sqlite3_bind_int (stmt, col++, 0);  // mapq
  sqlite3_bind_text (stmt, col++, "A", 1, SQLITE_STATIC);  // base
  sqlite3_bind_int (stmt, col++, 30);  // basequal
  sqlite3_bind_int64 (stmt, col++, 1);  // qpos
  sqlite3_bind_int (stmt, col++, 0);  // indel
  sqlite3_bind_int (stmt, col++, 0);  // is_del
  sqlite3_bind_int (stmt, col++, 0);  // is_head
  sqlite3_bind_int (stmt, col++, 0);  // is_tail
  sqlite3_bind_int (stmt, col++, 0);  // is_refskip
  sqlite3_bind_text (stmt, col++, "1M", -1, SQLITE_STATIC);  // cigar
  sqlite3_bind_text (stmt, col++, "A", 1, SQLITE_STATIC);  // seq
  sqlite3_bind_text (stmt, col++, "I", 1, SQLITE_STATIC);  // qual
  sqlite3_bind_null (stmt, col++);  // mtid
  sqlite3_bind_null (stmt, col++);  // mstart
  sqlite3_bind_null (stmt, col++);  // tags
  sqlite3_bind_blob (
      stmt, col++, cigBlob, 4, SQLITE_STATIC
  );  // cig_uint32
  sqlite3_bind_int (stmt, col++, 1);  // ncig

  return sqlite3_step (stmt);
}

}  // namespace

TEST_CASE ("metadata CHECK(id = 1) rejects a second row")
{
  PileupDB db = PileupDB::init();

  REQUIRE (insert_raw_metadata (db, 100, 100, 200) == SQLITE_DONE);

  const int rc = insert_raw_metadata (db, 100, 100, 200);
  CHECK ((rc & 0xFF) == SQLITE_CONSTRAINT);
}

TEST_CASE (
    "validate_read_span trigger enforces read span against locus "
    "metadata"
)
{
  PileupDB db = PileupDB::init();
  REQUIRE (insert_raw_metadata (db, 150, 100, 200) == SQLITE_DONE);

  SUBCASE ("span within locus bounds is accepted")
  {
    CHECK (insert_raw_read (db, 120, 170) == SQLITE_DONE);
  }

  SUBCASE ("read start before locus start is rejected")
  {
    const int rc = insert_raw_read (db, 50, 170);
    CHECK ((rc & 0xFF) == SQLITE_CONSTRAINT);
  }

  SUBCASE ("read end past locus end is rejected")
  {
    const int rc = insert_raw_read (db, 120, 250);
    CHECK ((rc & 0xFF) == SQLITE_CONSTRAINT);
  }

  SUBCASE ("read start past pileup pos is rejected")
  {
    const int rc = insert_raw_read (db, 160, 180);
    CHECK ((rc & 0xFF) == SQLITE_CONSTRAINT);
  }

  SUBCASE ("read end before pileup pos is rejected")
  {
    const int rc = insert_raw_read (db, 100, 140);
    CHECK ((rc & 0xFF) == SQLITE_CONSTRAINT);
  }
}
