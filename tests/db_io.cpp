#include "frontend/db_io.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <optional>
#include <string_view>

#include "doctest.h"
#include "fixtures.hpp"
#include "model/hts2apb.hpp"
#include "shared/cleanup.hpp"

TEST_CASE ("dump_to_disk writes a copy of the database")
{
  const auto path = test::temp_db_path ("apb-test-roundtrip.db");
  std::filesystem::remove (path);
  Defer removeFile ([&]() { std::filesystem::remove (path); });

  sqlite3* conn = nullptr;
  REQUIRE (apb_init_db (&conn) == SQLITE_OK);
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
  test::seed_reads (
      conn, 2, std::make_optional<std::string_view> ("ACGTACGT")
  );
  test::exec_ok (
      conn,
      "INSERT INTO alignment_files (path, has_contig, n_reads) "
      "VALUES ('other.bam', 0, 0);"
  );

  REQUIRE (dump_to_disk (conn, path.string()) == SQLITE_OK);

  sqlite3* dumped = nullptr;
  Defer closeDumped ([&]() { sqlite3_close_v2 (dumped); });
  REQUIRE (
      sqlite3_open_v2 (
          path.c_str(), &dumped, SQLITE_OPEN_READONLY, NULL
      ) == SQLITE_OK
  );

  for (const char* sql :
       {"SELECT * FROM reads ORDER BY id;",
        "SELECT * FROM alignment_files ORDER BY id;",
        "SELECT * FROM locus_metadata;"}) {
    CHECK (test::digest (dumped, sql) == test::digest (conn, sql));
  }
}

TEST_CASE ("dump_to_disk reports failure for an unwritable path")
{
  sqlite3* conn = nullptr;
  REQUIRE (apb_init_db (&conn) == SQLITE_OK);
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });

  const auto path = test::temp_db_path ("apb-no-such-dir/x.db");
  CHECK (dump_to_disk (conn, path.string()) != SQLITE_OK);
}
