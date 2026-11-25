#pragma once

#include <sqlite3.h>

#include <expected>
#include <functional>
#include <vector>

// Returns SQLITE_OK on success, sqlite err code on failure.
[[nodiscard]] int apb_copy_db (sqlite3* connSrc, sqlite3* connDest);

// Get names of all tables (view/virtual/shadow also)
// from `conn` via pragma_table_list
std::expected<std::vector<std::string>, int> apb_get_main_tables (
    sqlite3* conn
);

struct apb_col_spec_t {
  // enum corresponds to sqlite3 fundamental
  // datatype integer codes
  enum type_t : uint8_t {
    sqlite_int = 1,
    sqlite_float,
    sqlite_text,
    sqlite_blob,
    sqlite_null,
  };
  type_t type = sqlite_null;
  bool nullable = false;
  bool optional = false;
  // constraint to be checked after nullable/optional
  // handled. takes statement and position of column
  // in that statement and returns bool indicating
  // whether constraint is satisfied.
  std::function<bool (sqlite3_stmt*, int)> constraint = nullptr;
};
std::string to_string (const apb_col_spec_t::type_t& type);

struct apb_stmt_col_t {
  int index;
  std::string name;

  bool operator== (const apb_stmt_col_t&) const = default;
};
std::string to_string (const apb_stmt_col_t& col);

std::vector<apb_stmt_col_t> apb_get_columns (sqlite3_stmt* stmt);
