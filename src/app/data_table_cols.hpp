#pragma once

#include <sqlite3.h>

#include <functional>
#include <list>
#include <string>
#include <vector>


// TODO allow user
// to modify width
// to be greater than
// the minimum width
// (but not less)
struct DataTableCol {
  std::string_view name;
  size_t width;
  std::function<std::string (sqlite3_stmt* br_stmt)>
      retrieve_from_db;
};

enum class DataColID : uint8_t {
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

const DataTableCol* find_cols (std::string_view name);
using DataColList = std::list<const DataTableCol*>;
DataColList find_cols (const std::vector<DataColID>& ids);
