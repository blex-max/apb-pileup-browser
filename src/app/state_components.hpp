#pragma once

#include "backend/hts_sql.hpp"
#include "backend/schema.hpp"

struct ColMetadata {
  bool visible;
  const std::string_view fieldName;
  const uint16_t displayWidth;
  const std::function<std::string (sqlite3_stmt*)> fn_retrieve_from_db;
};

struct AppConfig {
  bool run = true;

  std::array<ColMetadata, 11> displayTableCols{{
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
         if (sqlite3_column_type (
                 row, schema::UserReadViewSelect::qname
             ) == SQLITE_NULL) {
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
         if (sqlite3_column_type (
                 row, schema::UserReadViewSelect::mstart
             ) == SQLITE_NULL) {
           return std::string ("*");
         }
         return std::to_string (
             sqlite3_column_int64 (row, schema::UserReadViewSelect::mstart)
         );
       }},
  }};

  bool showOverlay = false;
  struct {
    bool qual = false;
    bool ins = true;
  } drawTrackSwitches;
  struct {
    bool table = true;
  } drawPaneSwitches;
};

struct DBBundle {
  PileupDB db;
  query::DynamicFragments userClause{};
  // FIXME: finalisation?
  sqlite3_stmt* selectStmt = nullptr;
  uint32_t nStmtRows = 0;  // rows in current stmt
  int32_t stmtRowScrollOffset = 0;
  query::PileupMetadata
      locusInfo;  // cached metadata-table row; queried once at init(),
};
