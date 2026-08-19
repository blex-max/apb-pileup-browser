#include "data_table_cols.hpp"

#include <cstdint>

#include "backend/PileupDB.hpp"

static DataTableCol sh_colQname{"qname", 21, get_qname};

static DataTableCol sh_colFlag{
    "flag", 6, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_flag (br_stmt));
    }
};

static DataTableCol sh_colRstart{
    "rstart", 10, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_rstart (br_stmt));
    }
};

static DataTableCol sh_colRend{
    "rend", 10, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_rend (br_stmt));
    }
};

static DataTableCol sh_colMapq{
    "mapq", 6, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_mapq (br_stmt));
    }
};

static DataTableCol sh_colBaseQual{
    "basequal", 12, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_basequal (br_stmt));
    }
};

static DataTableCol sh_colQPos{
    "qpos", 6, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_qpos (br_stmt));
    }
};

static DataTableCol sh_colCigar{"cigar", 11, get_cigar};

static DataTableCol sh_colMTid{"mtid", 10, get_mtid};

static DataTableCol sh_colMStart{
    "mstart", 10, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_mstart (br_stmt));
    }
};

static DataTableCol sh_colTags{"tags", 20, get_tags};

static DataTableCol sh_colBase{
    "base", 6, [] (sqlite3_stmt* br_stmt) {
      return std::string{get_base (br_stmt)};
    }
};

static constexpr DataTableCol* sh_columnTable[]{
  // DO NOT REORDER (linked to enum)
    &sh_colQname, &sh_colFlag,     &sh_colRstart, &sh_colRend,
    &sh_colMapq,  &sh_colBaseQual, &sh_colQPos,   &sh_colCigar,
    &sh_colMTid,  &sh_colMStart,   &sh_colTags,   &sh_colBase
};

// NOTE: could map enum to string IDs rather than
// relying on ordering. But not an issue at present.

const DataTableCol* find_cols (std::string_view name)
{
  for (const auto* col : sh_columnTable) {
    if (col->name == name) {
      return col;
    }
  }
  return nullptr;
}

DataColList find_cols (const std::vector<DataColID>& ids)
{
  DataColList result;
  for (const auto& id : ids) {
    result.push_back (sh_columnTable[static_cast<uint8_t> (id)]);
  }
  return result;
}
