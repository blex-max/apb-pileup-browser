#pragma once

#include "backend/hts_sql.hpp"
#include "backend/schema.hpp"
#include "backend/sql_types.hpp"

struct ColMetadata {
  bool visible;
  const std::string_view fieldName;
  const uint16_t displayWidth;
  const std::function<std::string (sqlite3_stmt*)>
      fn_retrieve_from_db;
};

struct AppConfig {
  bool run = true;

  std::array<ColMetadata, 11> displayTableCols{{
      {false, "qpos", 6,
       [] (sqlite3_stmt* row) {
         return std::to_string (
             sqlite3_column_int (row, schema::ReadTableSelect::qpos)
         );
       }},
      {true, "basequal", 12,
       [] (sqlite3_stmt* row) {
         return std::to_string (
             sqlite3_column_int (row, schema::ReadTableSelect::basequal)
         );
       }},
      {true, "flag", 6,
       [] (sqlite3_stmt* row) {
         return std::to_string (
             sqlite3_column_int (row, schema::ReadTableSelect::flag)
         );
       }},
      {true, "mapq", 6,
       [] (sqlite3_stmt* row) {
         return std::to_string (
             sqlite3_column_int (row, schema::ReadTableSelect::mapq)
         );
       }},
      {false, "rstart", 14,
       [] (sqlite3_stmt* row) {
         return std::to_string (
             sqlite3_column_int64 (row, schema::ReadTableSelect::rstart)
         );
       }},
      {false, "rend", 14,
       [] (sqlite3_stmt* row) {
         return std::to_string (
             sqlite3_column_int64 (row, schema::ReadTableSelect::rend)
         );
       }},
      {true, "cigar", 16,
       [] (sqlite3_stmt* row) {
         const auto* p =
             sqlite3_column_text (row, schema::ReadTableSelect::cigar);
         const auto len =
             sqlite3_column_bytes (row, schema::ReadTableSelect::cigar);
         return std::string (
             reinterpret_cast<const char*> (p),
             static_cast<size_t> (len)
         );
       }},
      {false, "qname", 21,
       [] (sqlite3_stmt* row) {
         if (sqlite3_column_type (row, schema::ReadTableSelect::qname) ==
             SQLITE_NULL) {
           return std::string{"*"};
         }
         const auto* p =
             sqlite3_column_text (row, schema::ReadTableSelect::qname);
         const auto len =
             sqlite3_column_bytes (row, schema::ReadTableSelect::qname);
         return std::string (
             reinterpret_cast<const char*> (p),
             static_cast<size_t> (len)
         );
       }},
      {false, "mtid", 10,
       [] (sqlite3_stmt* row) {
         if (sqlite3_column_type (row, schema::ReadTableSelect::mtid) ==
             SQLITE_NULL) {
           return std::string{"*"};
         }
         const auto* p =
             sqlite3_column_text (row, schema::ReadTableSelect::mtid);
         const auto len =
             sqlite3_column_bytes (row, schema::ReadTableSelect::mtid);
         return std::string (
             reinterpret_cast<const char*> (p),
             static_cast<size_t> (len)
         );
       }},
      {false, "mstart", 14, [] (sqlite3_stmt* row) {
         if (sqlite3_column_type (row, schema::ReadTableSelect::mstart) ==
             SQLITE_NULL) {
           return std::string ("*");
         }
         return std::to_string (
             sqlite3_column_int64 (row, schema::ReadTableSelect::mstart)
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
  SqliteStmt selectStmt;
  uint32_t nStmtRows = 0;  // rows in current stmt
  int32_t stmtRowScrollOffset = 0;
  query::PileupMetadata
      locusInfo;  // cached metadata-table row; queried once at init(),
};
