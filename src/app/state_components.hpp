#pragma once

#include "backend/PileupDB.hpp"
#include "backend/schema.hpp"

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
             sqlite3_column_int (row, schema::FieldIndex::qpos)
         );
       }},
      {true, "basequal", 12,
       [] (sqlite3_stmt* row) {
         return std::to_string (sqlite3_column_int (
             row, schema::FieldIndex::basequal
         ));
       }},
      {true, "flag", 6,
       [] (sqlite3_stmt* row) {
         return std::to_string (
             sqlite3_column_int (row, schema::FieldIndex::flag)
         );
       }},
      {true, "mapq", 6,
       [] (sqlite3_stmt* row) {
         return std::to_string (
             sqlite3_column_int (row, schema::FieldIndex::mapq)
         );
       }},
      {false, "rstart", 14,
       [] (sqlite3_stmt* row) {
         return std::to_string (sqlite3_column_int64 (
             row, schema::FieldIndex::rstart
         ));
       }},
      {false, "rend", 14,
       [] (sqlite3_stmt* row) {
         return std::to_string (
             sqlite3_column_int64 (row, schema::FieldIndex::rend)
         );
       }},
      {true, "cigar", 16,
       [] (sqlite3_stmt* row) {
         const auto* p = sqlite3_column_text (
             row, schema::FieldIndex::cigar
         );
         const auto len = sqlite3_column_bytes (
             row, schema::FieldIndex::cigar
         );
         return std::string (
             reinterpret_cast<const char*> (p),
             static_cast<size_t> (len)
         );
       }},
      {false, "qname", 21,
       [] (sqlite3_stmt* row) {
         if (sqlite3_column_type (
                 row, schema::FieldIndex::qname
             ) == SQLITE_NULL) {
           return std::string{"*"};
         }
         const auto* p = sqlite3_column_text (
             row, schema::FieldIndex::qname
         );
         const auto len = sqlite3_column_bytes (
             row, schema::FieldIndex::qname
         );
         return std::string (
             reinterpret_cast<const char*> (p),
             static_cast<size_t> (len)
         );
       }},
      {false, "mtid", 10,
       [] (sqlite3_stmt* row) {
         if (sqlite3_column_type (
                 row, schema::FieldIndex::mtid
             ) == SQLITE_NULL) {
           return std::string{"*"};
         }
         const auto* p =
             sqlite3_column_text (row, schema::FieldIndex::mtid);
         const auto len = sqlite3_column_bytes (
             row, schema::FieldIndex::mtid
         );
         return std::string (
             reinterpret_cast<const char*> (p),
             static_cast<size_t> (len)
         );
       }},
      {false, "mstart", 14, [] (sqlite3_stmt* row) {
         if (sqlite3_column_type (
                 row, schema::FieldIndex::mstart
             ) == SQLITE_NULL) {
           return std::string ("*");
         }
         return std::to_string (sqlite3_column_int64 (
             row, schema::FieldIndex::mstart
         ));
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
  DynamicSelectReadsStmt stmt;
  DynamicFragments userClause{};
  uint32_t nStmtRows = 0;  // rows in current stmt
  int32_t stmtRowScrollOffset = 0;
  PileupMetadata
      locus;  // cached metadata-table row; queried once at init(),
};
