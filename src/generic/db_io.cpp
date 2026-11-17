#include "db_io.hpp"

#include <fmt/format.h>
#include <sqlite3.h>

#include "describe.hpp"
#include "model/apb_db.hpp"
#include "model/schema.hpp"
#include "utility/cleanup.hpp"

std::expected<void, dump_err_t> dump_to_disk (
    sqlite3* conn, std::string_view path
)
{
  sqlite3* o_dumpConn = nullptr;
  sqlite3_backup* o_backupConn = nullptr;
  Defer dumpCleanup ([&]() {
    sqlite3_backup_finish (o_backupConn);
    sqlite3_close_v2 (o_dumpConn);
  });

  if (const auto rc =
          sqlite3_open (std::string{path}.c_str(), &o_dumpConn);
      rc != SQLITE_OK) {
    return std::unexpected (dump_err_t{rc, sqlite3_errmsg (o_dumpConn)});
  }

  if (o_backupConn =
          sqlite3_backup_init (o_dumpConn, "main", conn, "main");
      o_backupConn == NULL) {
    return std::unexpected (
        dump_err_t{
            sqlite3_errcode (o_dumpConn), sqlite3_errmsg (o_dumpConn)
        }
    );
  }

  if (const auto rc = sqlite3_backup_step (o_backupConn, -1);
      rc != SQLITE_DONE) {
    return std::unexpected (dump_err_t{rc, sqlite3_errmsg (o_dumpConn)});
  }

  return {};
}

/* --- TESTS --- */
#ifndef DOCTEST_CONFIG_DISABLE

#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>

#include "doctest.h"
#include "fixtures.hpp"

namespace {

constexpr int64_t k_seedPos_0ho = 149;

const std::vector<test::TestRecord> k_seedReads{
    {.qname = "readA",
     .flag = 99,
     .mapQ = 60,
     .start_0ho = 99,
     .end_0ho = 170},
    {.qname = "readB",
     .flag = 4,
     .mapQ = 10,
     .start_0ho = 119,
     .end_0ho = 190},
    {.qname = "readC",
     .flag = 99,
     .mapQ = 45,
     .start_0ho = 139,
     .end_0ho = 200},
};

// dump a seeded apb db to `path`, optionally altering it first
void dump_seeded (
    const std::filesystem::path& path, const char* alterSql = nullptr
)
{
  sqlite3* conn = test::new_db();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
  const auto alns = std::to_array<test::TestAln> ({
      {"test.bam", k_seedReads, true},
  });
  test::seed_conn_with_reads (conn, alns, "chr1", k_seedPos_0ho, false);
  if (alterSql != nullptr) {
    test::exec_ok (conn, alterSql);
  }
  REQUIRE (dump_to_disk (conn, path.string()).has_value());
}

}  // namespace

TEST_CASE ("dump_to_disk then load_from_disk")
{
  const auto path = test::temp_db_path ("apb-test-load-roundtrip.db");
  std::filesystem::remove (path);
  Defer removeFile ([&]() { std::filesystem::remove (path); });

  sqlite3* conn = test::new_db();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
  const auto alns = std::to_array<test::TestAln> ({
      {"test.bam", k_seedReads, true},
      {"other.bam", std::nullopt, false},
  });
  test::seed_conn_with_reads (conn, alns, "chr1", k_seedPos_0ho, true);
  REQUIRE (dump_to_disk (conn, path.string()).has_value());

  auto loadRet = load_from_disk (path.string());
  REQUIRE (loadRet);
  sqlite3* loaded = *loadRet;
  Defer closeLoaded ([&]() { sqlite3_close_v2 (loaded); });

  for (const char* sql :
       {"SELECT * FROM reads ORDER BY id;",
        "SELECT * FROM alignment_files ORDER BY id;",
        "SELECT * FROM locus_metadata;"}) {
    CHECK (test::digest (loaded, sql) == test::digest (conn, sql));
  }
}

TEST_CASE ("load_from_disk reports a typed error for unusable files")
{
  const auto path = test::temp_db_path ("apb-test-unusable.db");
  std::filesystem::remove (path);
  Defer removeFile ([&]() { std::filesystem::remove (path); });

  SUBCASE ("sqlite file which is not an apb database")
  {
    sqlite3* other = nullptr;
    REQUIRE (
        sqlite3_open_v2 (
            path.c_str(), &other,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL
        ) == SQLITE_OK
    );
    Defer closeOther ([&]() { sqlite3_close_v2 (other); });
    test::exec_ok (other, "CREATE TABLE x (a);");

    const auto ret = load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().code == load_status_t::appMismatch);
  }

  SUBCASE ("apb database of another schema version")
  {
    dump_seeded (path, "PRAGMA user_version = 1;");
    const auto ret = load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().code == load_status_t::verMismatch);
  }

  SUBCASE ("apb database with altered schema")
  {
    dump_seeded (
        path, "ALTER TABLE locus_metadata ADD COLUMN extra TEXT;"
    );
    const auto ret = load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().code == load_status_t::schemaMismatch);
  }

  SUBCASE ("apb database with no locus metadata")
  {
    sqlite3* conn = test::new_db();
    Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
    REQUIRE (dump_to_disk (conn, path.string()).has_value());

    const auto ret = load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().code == load_status_t::contentCorrupt);
  }
}

#endif  // DOCTEST_CONFIG_DISABLE
