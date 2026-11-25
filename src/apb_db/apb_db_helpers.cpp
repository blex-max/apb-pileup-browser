#include "apb_db_helpers.hpp"

#include <fmt/format.h>
#include <sqlite3.h>

#include <cstddef>
#include <vector>

#include "utility/cleanup.hpp"

int apb_copy_db (sqlite3* connSrc, sqlite3* connDest)
{
  sqlite3_backup* o_backup = nullptr;
  Defer backup_cleanup ([&]() { sqlite3_backup_finish (o_backup); });

  if (o_backup = sqlite3_backup_init (connDest, "main", connSrc, "main");
      o_backup == NULL) {
    return sqlite3_errcode (connDest);
  }

  if (const auto rc = sqlite3_backup_step (o_backup, -1);
      rc != SQLITE_DONE) {
    return rc;
  }

  return SQLITE_OK;
}

std::expected<std::vector<std::string>, int> apb_get_main_tables (
    sqlite3* conn
)
{
  sqlite3_stmt* stmt;
  const char* sql =
      "SELECT name FROM pragma_table_list "
      "WHERE schema = 'main' AND name NOT LIKE 'sqlite_%'";

  if (const auto rc = sqlite3_prepare_v2 (conn, sql, -1, &stmt, NULL);
      rc != SQLITE_OK) {
    return std::unexpected (rc);
  }
  Defer stmt_cleanup ([&]() { sqlite3_finalize (stmt); });

  std::vector<std::string> out;
  int rc;
  while ((rc = sqlite3_step (stmt)) == SQLITE_ROW) {
    out.emplace_back (
        reinterpret_cast<const char*> (sqlite3_column_text (stmt, 0)),
        sqlite3_column_bytes (stmt, 0)
    );
  }
  if (rc != SQLITE_DONE) {
    return std::unexpected (rc);
  }

  return out;
}


std::string to_string (const apb_col_spec_t::type_t& type)
{
  switch (type) {
  case apb_col_spec_t::sqlite_int:
    return "INTEGER";
  case apb_col_spec_t::sqlite_float:
    return "FLOAT";
  case apb_col_spec_t::sqlite_blob:
    return "BLOB";
  case apb_col_spec_t::sqlite_text:
    return "TEXT";
  case apb_col_spec_t::sqlite_null:
    return "NULL";
  }
}

std::string to_string (const apb_stmt_col_t& col)
{
  return fmt::format ("apb_stmt_col_t{{{}, {}}}", col.index, col.name);
}

std::vector<apb_stmt_col_t> apb_get_columns (sqlite3_stmt* stmt)
{
  std::vector<apb_stmt_col_t> out;
  const auto n = sqlite3_column_count (stmt);
  out.reserve (static_cast<size_t> (n));
  for (int i = 0; i < n; ++i) {
    out.emplace_back (i, sqlite3_column_name (stmt, i));
  }
  return out;
}


#ifndef DOCTEST_CONFIG_DISABLE

#include "doctest.h"

// I hate cpp
namespace doctest {
template <>
struct StringMaker<apb_stmt_col_t> {
  static String convert (const apb_stmt_col_t& s)
  {
    return to_string (s).c_str();
  }
};
}  // namespace doctest

TEST_CASE ("apb_get_* behaviour")
{
  sqlite3* o_conn;
  // clang-format off
  constexpr std::string_view sqlTestSchema =
  R"sql(
CREATE TABLE test (
  id INTEGER PRIMARY KEY,
  my_int INTEGER,
  my_float FLOAT,
  my_text TEXT,
  my_blob BLOB,
  my_null NULL
);
  )sql";
  // clang-format on
  REQUIRE (sqlite3_open (":memory:", &o_conn) == SQLITE_OK);
  Defer conn_cleanup ([&]() { sqlite3_close_v2 (o_conn); });
  REQUIRE (
      sqlite3_exec (o_conn, sqlTestSchema.data(), NULL, NULL, NULL) ==
      SQLITE_OK
  );

  SUBCASE ("apb_get_tables")
  {
    auto ret = apb_get_main_tables (o_conn);
    REQUIRE (ret);
    REQUIRE (*ret == std::vector<std::string>{"test"});
  }

  SUBCASE ("apb_get_columns")
  {
    const std::string_view sqlSelectAll = R"sql(SELECT * FROM test;)sql";
    sqlite3_stmt* o_stmt;
    REQUIRE (
        sqlite3_prepare_v2 (
            o_conn, sqlSelectAll.data(), -1, &o_stmt, NULL
        ) == SQLITE_OK
    );
    Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });
    REQUIRE (
        apb_get_columns (o_stmt) == std::vector<apb_stmt_col_t>{
                                        {0, "id"},
                                        {1, "my_int"},
                                        {2, "my_float"},
                                        {3, "my_text"},
                                        {4, "my_blob"},
                                        {5, "my_null"},
                                    }
    );
  }
}


#endif
