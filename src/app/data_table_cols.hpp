#pragma once

#include <sqlite3.h>

#include <expected>
#include <functional>
#include <string>

namespace TableCol {

enum class ID : uint8_t {
  // DO NOT REORDER
  qname = 0,
  flag,
  rstart,
  rend,
  mapq,
  basequal,
  qpos,
  cigar,
  mtid,
  mstart,
  tags,
  base
};

struct ColMetadata {
  const ID id;
  const std::string_view fieldName;
  const uint16_t displayWidth;
  std::function<std::string (sqlite3_stmt* br_stmt)>
      fn_retrieve_from_db;
};


const ColMetadata* get_metadata_by_id (ID id);
std::expected<ID, std::nullopt_t> get_id_by_name (
    std::string_view name
);

}  // namespace TableCol
