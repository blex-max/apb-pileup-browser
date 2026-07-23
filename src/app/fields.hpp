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

inline TableField qname{"qname", 21, get_qname};

inline TableField flag{"flag", 6, [] (sqlite3_stmt* stmt) {
                         return std::to_string (get_flag (stmt));
                       }};

inline TableField rstart{
    "rstart", 10, [] (sqlite3_stmt* stmt) {
      return std::to_string (get_rstart (stmt));
    }
};

inline TableField rend{"rend", 10, [] (sqlite3_stmt* stmt) {
                         return std::to_string (get_rend (stmt));
                       }};

inline TableField mapq{"mapq", 6, [] (sqlite3_stmt* stmt) {
                         return std::to_string (get_mapq (stmt));
                       }};

inline TableField basequal{
    "basequal", 12, [] (sqlite3_stmt* stmt) {
      return std::to_string (get_basequal (stmt));
    }
};

inline TableField qpos{"qpos", 6, [] (sqlite3_stmt* stmt) {
                         return std::to_string (get_qpos (stmt));
                       }};

inline TableField cigar{"cigar", 11, get_cigar};

inline TableField mtid{"mtid", 10, get_mtid};

inline TableField mstart{
    "mstart", 10, [] (sqlite3_stmt* stmt) {
      return std::to_string (get_mstart (stmt));
    }
};

inline TableField tags{"tags", 20, get_tags};

inline TableField base{"base", 6, [] (sqlite3_stmt* stmt) {
                         return std::string{get_base (stmt)};
                       }};

}  // namespace fields

inline const std::unordered_map<
    std::string_view, const TableField*>
    TABLE_FIELD_LOOKUP{{"qname", &fields::qname},
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
                       {"base", &fields::base}};
