#include "app/g_state.hpp"

#include <string>
#include <utility>

#include "backend/schema.hpp"

namespace g_state::ui {

constinit std::array<ColMetadata, 11> tableCols{{
    {false, "qpos", 6,
     [] (sqlite3_stmt* row) {
       return std::to_string (
           sqlite3_column_int (row, schema::UserReadViewSelect::qpos)
       );
     }},
    {true, "basequal", 12,
     [] (sqlite3_stmt* row) {
       return std::to_string (
           sqlite3_column_int (row, schema::UserReadViewSelect::basequal)
       );
     }},
    {true, "flag", 6,
     [] (sqlite3_stmt* row) {
       return std::to_string (
           sqlite3_column_int (row, schema::UserReadViewSelect::flag)
       );
     }},
    {true, "mapq", 6,
     [] (sqlite3_stmt* row) {
       return std::to_string (
           sqlite3_column_int (row, schema::UserReadViewSelect::mapq)
       );
     }},
    {false, "start", 14,
     [] (sqlite3_stmt* row) {
       return std::to_string (
           sqlite3_column_int64 (row, schema::UserReadViewSelect::start)
       );
     }},
    {false, "end", 14,
     [] (sqlite3_stmt* row) {
       return std::to_string (
           sqlite3_column_int64 (row, schema::UserReadViewSelect::end)
       );
     }},
    {true, "cigar", 16,
     [] (sqlite3_stmt* row) {
       const auto* p =
           sqlite3_column_text (row, schema::UserReadViewSelect::cigar);
       const auto len =
           sqlite3_column_bytes (row, schema::UserReadViewSelect::cigar);
       return std::string (
           reinterpret_cast<const char*> (p), static_cast<size_t> (len)
       );
     }},
    {false, "qname", 21,
     [] (sqlite3_stmt* row) {
       if (sqlite3_column_type (row, schema::UserReadViewSelect::qname) ==
           SQLITE_NULL) {
         return std::string{"*"};
       }
       const auto* p =
           sqlite3_column_text (row, schema::UserReadViewSelect::qname);
       const auto len =
           sqlite3_column_bytes (row, schema::UserReadViewSelect::qname);
       return std::string (
           reinterpret_cast<const char*> (p), static_cast<size_t> (len)
       );
     }},
    {false, "mtid", 10,
     [] (sqlite3_stmt* row) {
       if (sqlite3_column_type (row, schema::UserReadViewSelect::mtid) ==
           SQLITE_NULL) {
         return std::string{"*"};
       }
       const auto* p =
           sqlite3_column_text (row, schema::UserReadViewSelect::mtid);
       const auto len =
           sqlite3_column_bytes (row, schema::UserReadViewSelect::mtid);
       return std::string (
           reinterpret_cast<const char*> (p), static_cast<size_t> (len)
       );
     }},
    {false, "mstart", 14, [] (sqlite3_stmt* row) {
       if (sqlite3_column_type (row, schema::UserReadViewSelect::mstart) ==
           SQLITE_NULL) {
         return std::string ("*");
       }
       return std::to_string (
           sqlite3_column_int64 (row, schema::UserReadViewSelect::mstart)
       );
     }},
}};

}  // namespace g_state::ui

namespace g_state::db {

SetQueryStatus set_query (query::DynamicFragments clause)
{
  auto prepResult = query::prepare_select_reads (
      conn, schema::UserReadViewSelect::sqlPrefix, clause
  );
  if (!prepResult) {
    return {.code = SetQueryStatus::prepareFail, .rc = prepResult.error()};
  }
  auto* newStmt = *prepResult;
  const auto rowCountResult = query::count_rows (newStmt);
  if (!rowCountResult) {
    sqlite3_finalize (newStmt);
    return {
        .code = SetQueryStatus::countFail, .rc = rowCountResult.error()
    };
  }
  sqlite3_finalize (selectStmt);
  selectStmt = newStmt;
  userClause = std::move (clause);
  stmtRowScrollOffset = 0;  // reset row view
  nStmtRows = *rowCountResult;
  return {.code = SetQueryStatus::success};
}

}  // namespace g_state::db
