#include "data_table_cols.hpp"

#include <optional>

#include "backend/PileupDB.hpp"

namespace TableCol {

static ColMetadata sh_colQname{
    ID::qname, "qname", 21, get_qname
};

static ColMetadata sh_colFlag{
    ID::flag, "flag", 6, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_flag (br_stmt));
    }
};

static ColMetadata sh_colRstart{
    ID::rstart, "rstart", 10, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_rstart (br_stmt));
    }
};

static ColMetadata sh_colRend{
    ID::rend, "rend", 10, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_rend (br_stmt));
    }
};

static ColMetadata sh_colMapq{
    ID::mapq, "mapq", 6, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_mapq (br_stmt));
    }
};

static ColMetadata sh_colBaseQual{
    ID::basequal, "basequal", 12, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_basequal (br_stmt));
    }
};

static ColMetadata sh_colQPos{
    ID::qpos, "qpos", 6, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_qpos (br_stmt));
    }
};

static ColMetadata sh_colCigar{
    ID::cigar, "cigar", 11, get_cigar
};

static ColMetadata sh_colMTid{ID::mtid, "mtid", 10, get_mtid};

static ColMetadata sh_colMStart{
    ID::mstart, "mstart", 10, [] (sqlite3_stmt* br_stmt) {
      return std::to_string (get_mstart (br_stmt));
    }
};

static ColMetadata sh_colTags{ID::tags, "tags", 20, get_tags};

static ColMetadata sh_colBase{
    ID::base, "base", 6, [] (sqlite3_stmt* br_stmt) {
      return std::string{get_base (br_stmt)};
    }
};

static constexpr ColMetadata* sh_columnTable[]{
  // DO NOT REORDER (linked to enum)
    &sh_colQname, &sh_colFlag,     &sh_colRstart, &sh_colRend,
    &sh_colMapq,  &sh_colBaseQual, &sh_colQPos,   &sh_colCigar,
    &sh_colMTid,  &sh_colMStart,   &sh_colTags,   &sh_colBase
};

const ColMetadata* get_metadata_by_id (ID id)
{
  for (const auto* col : sh_columnTable) {
    if (col->id == id) {
      return col;
    }
  }

  assert (
      false && "All IDs should have TableCol implmentations"
  );
  return nullptr;
}

std::expected<ID, std::nullopt_t> get_id_by_name (
    std::string_view name
)
{
  for (const auto* col : sh_columnTable) {
    if (col->fieldName == name) {
      return col->id;
    }
  }

  return std::unexpected (std::nullopt);
}

}  // namespace TableCol
