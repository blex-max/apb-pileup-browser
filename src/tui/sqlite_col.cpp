#include "sqlite_col.hpp"

#include "apb_db/apb_db_helpers.hpp"
#include "tui/tui_assert.hpp"

std::optional<std::string> col_text (sqlite3_stmt* stmt, int idx)
{
  if (const auto colType = static_cast<apb_col_type_t::type_t> (
          sqlite3_column_type (stmt, idx)
      );
      colType == apb_col_type_t::sqlite_null) {
    return std::nullopt;
  }
  else {
    TUI_ASSERT (colType == apb_col_type_t::sqlite_text);
  }
  // text before bytes, per sqlite3 docs
  const auto* p = sqlite3_column_text (stmt, idx);
  const auto len = sqlite3_column_bytes (stmt, idx);
  return std::string (
      reinterpret_cast<const char*> (p), static_cast<size_t> (len)
  );
}

std::optional<std::string> col_to_string (sqlite3_stmt* stmt, int idx)
{
  switch (static_cast<apb_col_type_t::type_t> (
      sqlite3_column_type (stmt, idx)
  )) {
  case apb_col_type_t::sqlite_int:
    return std::to_string (sqlite3_column_int64 (stmt, idx));
  case apb_col_type_t::sqlite_float:
    return std::to_string (sqlite3_column_double (stmt, idx));
  case apb_col_type_t::sqlite_text:
    return std::string (
        reinterpret_cast<const char*> (sqlite3_column_text (stmt, idx)),
        static_cast<size_t> (sqlite3_column_bytes (stmt, idx))
    );
  case apb_col_type_t::sqlite_blob:
    return "NA";
  case apb_col_type_t::sqlite_null:
    return std::nullopt;
  }
}
