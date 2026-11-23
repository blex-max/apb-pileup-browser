// NULL-aware sqlite3 column reads.
// sqlite3_column_int* reads NULL as 0, and
// sqlite3_column_text reads NULL as nullptr.
#pragma once

#include <sqlite3.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// nullopt if SQLITE_NULL
inline std::optional<int64_t> col_int64 (sqlite3_stmt* stmt, int idx)
{
  if (sqlite3_column_type (stmt, idx) == SQLITE_NULL) {
    return std::nullopt;
  }
  return sqlite3_column_int64 (stmt, idx);
}

// nullopt if SQLITE_NULL
inline std::optional<std::string> col_text (sqlite3_stmt* stmt, int idx)
{
  if (sqlite3_column_type (stmt, idx) == SQLITE_NULL) {
    return std::nullopt;
  }
  // text before bytes, per sqlite3 docs
  const auto* p = sqlite3_column_text (stmt, idx);
  const auto len = sqlite3_column_bytes (stmt, idx);
  return std::string (
      reinterpret_cast<const char*> (p), static_cast<size_t> (len)
  );
}
