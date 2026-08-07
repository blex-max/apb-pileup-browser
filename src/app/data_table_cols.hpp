#pragma once

#include <sqlite3.h>

#include <functional>
#include <string>

#include "backend/PileupDB.hpp"

// TODO allow user
// to modify width
// to be greater than
// the minimum width
// (but not less)
struct DataTableCol {
  std::string name;
  size_t width;
  std::function<std::string (sqlite3_stmt* stmt)>
      retrieve_from_db;
};

namespace cols {

inline DataTableCol qname{"qname", 21, get_qname};

inline DataTableCol flag{
    "flag", 6, [] (sqlite3_stmt* stmt) {
      return std::to_string (get_flag (stmt));
    }
};

inline DataTableCol rstart{
    "rstart", 10, [] (sqlite3_stmt* stmt) {
      return std::to_string (get_rstart (stmt));
    }
};

inline DataTableCol rend{
    "rend", 10, [] (sqlite3_stmt* stmt) {
      return std::to_string (get_rend (stmt));
    }
};

inline DataTableCol mapq{
    "mapq", 6, [] (sqlite3_stmt* stmt) {
      return std::to_string (get_mapq (stmt));
    }
};

inline DataTableCol basequal{
    "basequal", 12, [] (sqlite3_stmt* stmt) {
      return std::to_string (get_basequal (stmt));
    }
};

inline DataTableCol qpos{
    "qpos", 6, [] (sqlite3_stmt* stmt) {
      return std::to_string (get_qpos (stmt));
    }
};

inline DataTableCol cigar{"cigar", 11, get_cigar};

inline DataTableCol mtid{"mtid", 10, get_mtid};

inline DataTableCol mstart{
    "mstart", 10, [] (sqlite3_stmt* stmt) {
      return std::to_string (get_mstart (stmt));
    }
};

inline DataTableCol tags{"tags", 20, get_tags};

inline DataTableCol base{"base", 6, [] (sqlite3_stmt* stmt) {
                           return std::string{get_base (stmt)};
                         }};

inline constexpr DataTableCol* COLUMN_TABLE[]{
    &qname, &flag,  &rstart, &rend,   &mapq, &basequal,
    &qpos,  &cigar, &mtid,   &mstart, &tags, &base
};

}  // namespace cols

constexpr const DataTableCol* find_col (std::string_view name)
{
  for (const auto* col : cols::COLUMN_TABLE) {
    if (col->name == name) {
      return col;
    }
  }
  return nullptr;
}
