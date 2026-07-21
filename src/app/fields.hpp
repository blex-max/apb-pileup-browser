#pragma once

#include <sqlite3.h>

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "backend/PileupDB.hpp"

// TODO allow user
// to modify width
// to be greater than
// the minimum width
// (but not less)
struct TableField {
  std::string name;
  size_t width;
  std::function<std::string (sqlite3_stmt* stmt)>
      retrieve_from_db;
};

namespace fields {
static TableField qname{"qname", 21, [] (sqlite3_stmt* stmt) {
                          return get_qname (stmt);
                        }};
static TableField flag{"flag", 6, [] (sqlite3_stmt* stmt) {
                         return std::to_string (get_flag (stmt));
                       }};
static TableField rstart{
    "rstart", 10, [] (sqlite3_stmt* stmt) {
      return std::to_string (get_rstart (stmt));
    }
};
static TableField rend{"rend", 10, [] (sqlite3_stmt* stmt) {
                         return std::to_string (get_rend (stmt));
                       }};
static TableField mapq{"mapq", 6, [] (sqlite3_stmt* stmt) {
                         return std::to_string (get_mapq (stmt));
                       }};
static TableField basequal{
    "basequal", 12, [] (sqlite3_stmt* stmt) {
      return std::to_string (get_basequal (stmt));
    }
};
static TableField qpos{"qpos", 6, [] (sqlite3_stmt* stmt) {
                         return std::to_string (get_qpos (stmt));
                       }};
static TableField cigar{"cigar", 11, [] (sqlite3_stmt* stmt) {
                          return get_cigar (stmt);
                        }};
static TableField mtid{"mtid", 10, [] (sqlite3_stmt* stmt) {
                         return get_mtid (stmt);
                       }};
static TableField mstart{
    "mstart", 10, [] (sqlite3_stmt* stmt) {
      return std::to_string (get_mstart (stmt));
    }
};
static TableField tags{"tags", 20, [] (sqlite3_stmt* stmt) {
                         return get_tags (stmt);
                       }};
}  // namespace fields

static const std::unordered_map<
    std::string_view, const TableField*>
    TABLE_FIELD_LOOKUP{
        {"qname", &fields::qname},
        {"flag", &fields::flag},
        {"rstart", &fields::rstart},
        {"rend", &fields::rend},
        {"mapq", &fields::mapq},
        {"basequal", &fields::basequal},
        {"qpos", &fields::qpos},
        {"cigar", &fields::cigar},
        {"mtid", &fields::mtid},
        {"mstart", &fields::mstart},
        {"tags", &fields::tags},
    };
