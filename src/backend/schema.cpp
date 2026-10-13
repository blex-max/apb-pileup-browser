#include "backend/schema.hpp"

#include <sqlite3.h>

#include <cstddef>
#include <cstdint>

#include "backend/hts_sql.hpp"
#include "doctest.h"
#include "shared/cleanup.hpp"

// Test apb schema constraints

namespace {

// partially fixed values for metadata table insertion
int insert_raw_metadata (
    sqlite3* conn, int64_t pos, int64_t start, int64_t end
)
{
  sqlite3_stmt* stmt;
  REQUIRE (
      sqlite3_prepare_v2 (
          conn, schema::sqlInsertMetadata.data(),
          static_cast<int> (schema::sqlInsertMetadata.size()), &stmt, NULL
      ) == SQLITE_OK
  );

  int col = 1;
  sqlite3_bind_int64 (stmt, col++, kSchemaVersion);
  sqlite3_bind_text (stmt, col++, "chr1", -1, SQLITE_STATIC);
  sqlite3_bind_int64 (stmt, col++, pos);
  sqlite3_bind_int64 (stmt, col++, start);
  sqlite3_bind_int64 (stmt, col++, end);
  sqlite3_bind_null (stmt, col++);
  sqlite3_bind_int (stmt, col++, 1);  // n_aln

  return sqlite3_step (stmt);
}

}  // namespace

TEST_CASE ("metadata CHECK(id = 1) rejects a second row")
{
  sqlite3* conn = setup_db::init();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });

  REQUIRE (insert_raw_metadata (conn, 100, 100, 200) == SQLITE_DONE);

  const int rc = insert_raw_metadata (conn, 100, 100, 200);
  CHECK ((rc & 0xFF) == SQLITE_CONSTRAINT);
}
