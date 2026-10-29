#include "db_io.hpp"

#include <fmt/format.h>
#include <sqlite3.h>

#include "describe.hpp"
#include "model/hts2apb.hpp"
#include "model/schema.hpp"
#include "shared/cleanup.hpp"

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

std::expected<sqlite3*, load_err_t> load_from_disk (std::string_view path)
{
  sqlite3* conn = nullptr;
  // closes conn on every failure return; cancelled on success
  Defer connCleanup ([&]() { sqlite3_close_v2 (conn); });
  if (const auto rc = sqlite3_open (":memory:", &conn); rc != SQLITE_OK) {
    return std::unexpected (
        load_err_t{
            .kind = load_err_t::sqlFail,
            .rc = rc,
            .msg = sqlite3_errmsg (conn)
        }
    );
  }

  {
    sqlite3* o_fileDb = nullptr;
    sqlite3_backup* o_backup = nullptr;
    Defer cleanup ([&]() {
      sqlite3_backup_finish (o_backup);
      sqlite3_close_v2 (o_fileDb);
    });

    if (const auto rc = sqlite3_open_v2 (
            std::string{path}.c_str(), &o_fileDb, SQLITE_OPEN_READONLY,
            NULL
        );
        rc != SQLITE_OK) {
      // the error belongs to o_fileDb, the handle that failed to open
      return std::unexpected (
          load_err_t{
              .kind = load_err_t::openFail,
              .rc = rc,
              .msg = sqlite3_errmsg (o_fileDb)
          }
      );
    }

    // per sqlite3 docs, errors from backup_init/backup_step are
    // stored on the destination handle, i.e. conn.
    if (o_backup = sqlite3_backup_init (conn, "main", o_fileDb, "main");
        o_backup == NULL) {
      return std::unexpected (
          load_err_t{
              .kind = load_err_t::copyFail,
              .rc = sqlite3_errcode (conn),
              .msg = sqlite3_errmsg (conn)
          }
      );
    }
    if (const auto rc = sqlite3_backup_step (o_backup, -1);
        rc != SQLITE_DONE) {
      return std::unexpected (
          load_err_t{
              .kind = load_err_t::copyFail,
              .rc = rc,
              .msg = sqlite3_errmsg (conn)
          }
      );
    }
  }

  // connection settings (not stored in the file)
  for (const auto pragma :
       {schema::sqlSetTempStoreMemory, schema::sqlSetFK}) {
    if (const auto rc = sqlite3_exec (
            conn, std::string{pragma}.c_str(), NULL, NULL, NULL
        );
        rc != SQLITE_OK) {
      return std::unexpected (
          load_err_t{
              .kind = load_err_t::sqlFail,
              .rc = rc,
              .msg = sqlite3_errmsg (conn)
          }
      );
    }
  }

  {
    // verify database is not corrupt
    sqlite3_stmt* o_stmt = nullptr;
    if (const auto rc = sqlite3_prepare_v2 (
            conn, "PRAGMA quick_check;", -1, &o_stmt, NULL
        );
        rc != SQLITE_OK) {
      return std::unexpected (
          load_err_t{
              .kind = load_err_t::contentCorrupt,
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
    }
    if (!problems.empty()) {
      return std::unexpected (
          load_err_t{.kind = load_err_t::contentCorrupt, .msg = problems}
      );
    }
  }

  const auto verifyRet = apb_verify_schema (conn);
  if (!verifyRet) {
    return std::unexpected (
        load_err_t{
            .kind = load_err_t::sqlFail,
            .rc = verifyRet.error(),
            .msg = sqlite3_errmsg (conn)
        }
    );
  }
  switch (*verifyRet) {
    case apb_verify_result_t::success:
      break;
    case apb_verify_result_t::appMismatch:
      return std::unexpected (load_err_t{.kind = load_err_t::appMismatch});
    case apb_verify_result_t::verMismatch:
      return std::unexpected (load_err_t{.kind = load_err_t::verMismatch});
    case apb_verify_result_t::schemaMismatch:
      return std::unexpected (
          load_err_t{.kind = load_err_t::schemaMismatch}
      );
  }

  {
    // Verify locus metadata row exists - the one thing schema
    // matching and quick_check don't catch.
    sqlite3_stmt* o_stmt = nullptr;
    if (const auto rc = sqlite3_prepare_v2 (
            conn, schema::MetaTableSelect::sql.data(),
            static_cast<int> (schema::MetaTableSelect::sql.size()),
            &o_stmt, NULL
        );
        rc != SQLITE_OK) {
      return std::unexpected (
          load_err_t{
              .kind = load_err_t::sqlFail,
              .rc = rc,
              .msg = sqlite3_errmsg (conn)
          }
      );
    }
    Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

    switch (const auto rc = sqlite3_step (o_stmt); rc) {
      case SQLITE_ROW:
        break;
      case SQLITE_DONE:
        return std::unexpected (
            load_err_t{
                .kind = load_err_t::contentCorrupt,
                .msg = "database has no locus metadata - is this a valid "
                       "apb dump?"
            }
        );
      default:
        return std::unexpected (
            load_err_t{
                .kind = load_err_t::sqlFail,
                .rc = rc,
                .msg = sqlite3_errmsg (conn)
            }
        );
    }
  }

  connCleanup.cancel();
  return conn;
}

// Formats a load_from_disk failure into a user-facing error.
std::string apb_describe_load_failure (
    std::string_view path, const load_err_t& err
)
{
  switch (err.kind) {
    case load_err_t::openFail:
      return fmt::format (
          "failed to open database at {}, reporting error: {}; and "
          "extended status msg: {}",
          path, sqlite3_errstr (err.rc.value()), err.msg.value()
      );
    case load_err_t::copyFail:
      return apb_describe_sqlite_failure (
          "Failed to load database", err.rc.value(), err.msg
      );
    case load_err_t::contentCorrupt:
      return fmt::format (
          "database at {} appears to be corrupt:\n{}", path,
          err.msg.value()
      );
    case load_err_t::appMismatch:
      return fmt::format ("database at {} is not an apb database.", path);
    case load_err_t::verMismatch:
      return fmt::format (
          "database at {} is from a different version of apb's database "
          "schema. Is it from an old version?",
          path
      );
    case load_err_t::schemaMismatch:
      return fmt::format (
          "database at {} does not have the expected schema for an apb "
          "database.",
          path
      );
    case load_err_t::sqlFail:
      return apb_describe_sqlite_failure (
          "Failed to verify database", err.rc.value_or (SQLITE_ERROR),
          err.msg
      );
  }
  return {};
}

/* --- TESTS --- */
#ifndef DOCTEST_CONFIG_DISABLE

#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>

#include "doctest.h"
#include "fixtures.hpp"

namespace {

// dump a seeded apb db to `path`, optionally altering it first
void dump_seeded (
    const std::filesystem::path& path, const char* alterSql = nullptr
)
{
  sqlite3* conn = test::new_db();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
  test::seed_reads (conn);
  if (alterSql != nullptr) {
    test::exec_ok (conn, alterSql);
  }
  REQUIRE (dump_to_disk (conn, path.string()).has_value());
}

}  // namespace

TEST_CASE ("dump_to_disk then load_from_disk round-trips a database")
{
  const auto path = test::temp_db_path ("apb-test-load-roundtrip.db");
  std::filesystem::remove (path);
  Defer removeFile ([&]() { std::filesystem::remove (path); });

  sqlite3* conn = test::new_db();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
  test::seed_reads (conn, {{"other.bam", false, 0}}, true);
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

  SUBCASE ("nonexistent file")
  {
    const auto ret = load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().kind == load_err_t::openFail);
  }

  SUBCASE ("file which is not a sqlite3 database")
  {
    std::ofstream (path) << "this is definitely not a database, at all.\n"
                         << std::string (200, 'x');
    const auto ret = load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().kind == load_err_t::copyFail);
  }

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
    CHECK (ret.error().kind == load_err_t::appMismatch);
  }

  SUBCASE ("apb database of another schema version")
  {
    dump_seeded (path, "PRAGMA user_version = 1;");
    const auto ret = load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().kind == load_err_t::verMismatch);
  }

  SUBCASE ("apb database with altered schema")
  {
    dump_seeded (
        path, "ALTER TABLE locus_metadata ADD COLUMN extra TEXT;"
    );
    const auto ret = load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().kind == load_err_t::schemaMismatch);
  }

  SUBCASE ("apb database with no locus metadata")
  {
    sqlite3* conn = test::new_db();
    Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
    REQUIRE (dump_to_disk (conn, path.string()).has_value());

    const auto ret = load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().kind == load_err_t::contentCorrupt);
  }
}

TEST_CASE (
    "load_from_disk tolerates schema changes which keep the columns"
)
{
  const auto path = test::temp_db_path ("apb-test-tolerant.db");
  std::filesystem::remove (path);
  Defer removeFile ([&]() { std::filesystem::remove (path); });

  dump_seeded (
      path,
      "DROP VIEW reads;"
      "CREATE VIEW reads AS SELECT r.*, f.path FROM per_record_data r "
      "JOIN alignment_files f ON f.id = r.path_id WHERE r.mapq > 0;"
  );
  auto loadRet = load_from_disk (path.string());
  REQUIRE (loadRet);
  Defer closeLoaded ([&]() { sqlite3_close_v2 (*loadRet); });
}

TEST_CASE ("load_from_disk loads a database with a non-default page size")
{
  const auto path = test::temp_db_path ("apb-test-pagesize.db");
  std::filesystem::remove (path);
  Defer removeFile ([&]() { std::filesystem::remove (path); });

  {
    // build an apb database directly on disk, as another tool might
    sqlite3* fileDb = nullptr;
    REQUIRE (sqlite3_open (path.c_str(), &fileDb) == SQLITE_OK);
    Defer closeFileDb ([&]() { sqlite3_close_v2 (fileDb); });
    test::exec_ok (fileDb, "PRAGMA page_size = 8192;");
    REQUIRE (apb_create_schema (fileDb) == SQLITE_OK);
    test::seed_reads (fileDb);
    REQUIRE (test::digest (fileDb, "PRAGMA page_size;") == "8192|\n");
  }

  auto loadRet = load_from_disk (path.string());
  REQUIRE (loadRet);
  sqlite3* loaded = *loadRet;
  Defer closeLoaded ([&]() { sqlite3_close_v2 (loaded); });
  CHECK (test::digest (loaded, "SELECT count(*) FROM reads;") == "3|\n");
  // connection settings apply to the loaded db
  CHECK (test::digest (loaded, "PRAGMA foreign_keys;") == "1|\n");
}

#endif  // DOCTEST_CONFIG_DISABLE
