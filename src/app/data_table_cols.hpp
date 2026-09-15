#pragma once

#include <sqlite3.h>

#include <functional>
#include <string>

#include "backend/PileupDB.hpp"


namespace TableCol {

struct ColMetadata {
  const std::string_view fieldName;
  const uint16_t displayWidth;
  std::function<std::string (sqlite3_stmt* br_stmt)>
      fn_retrieve_from_db;
};

inline ColMetadata sh_colQname{"qname", 21, get_qname};

inline ColMetadata sh_colFlag{
    "flag", 6, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_flag (br_stmt));
    }
};

inline ColMetadata sh_colRstart{
    "rstart", 10, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_rstart (br_stmt));
    }
};

inline ColMetadata sh_colRend{
    "rend", 10, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_rend (br_stmt));
    }
};

inline ColMetadata sh_colMapq{
    "mapq", 6, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_mapq (br_stmt));
    }
};

inline ColMetadata sh_colBaseQual{
    "basequal", 12, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_basequal (br_stmt));
    }
};

inline ColMetadata sh_colQPos{
    "qpos", 6, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_qpos (br_stmt));
    }
};

inline ColMetadata sh_colCigar{"cigar", 11, get_cigar};

inline ColMetadata sh_colMTid{"mtid", 10, get_mtid};

inline ColMetadata sh_colMStart{
    "mstart", 10, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_mstart (br_stmt));
    }
};

inline ColMetadata sh_colTags{"tags", 20, get_tags};

}  // namespace TableCol
