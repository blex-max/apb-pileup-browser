#pragma once

#include <list>

#include "app/data_table_cols.hpp"
#include "backend/PileupDB.hpp"
#include "frontend/extb/extb.hpp"


constexpr double DEFAULT_SEQ_PANE_FRAC = 0.5;
using DataRequestList = std::list<const DataTableCol*>;
struct AppConfig {
  bool run = true;
  DataRequestList colsRequested{
      &cols::basequal, &cols::rstart, &cols::rend, &cols::flag,
      &cols::mapq,     &cols::cigar,  &cols::qname
  };  // preseves insertion order, allows removal by val
  double seqPaneFrac = DEFAULT_SEQ_PANE_FRAC;
  bool showOverlay = false;
};

struct AppMetadata {
  size_t c_frame = 0;  // c_ == counter
  tb_event lastEv{};
};

struct DBBundle {
  PileupDB db;
  DynamicSelectReadsStmt stmt;
  DynamicFragments userClause{};
  LocusData
      locus;  // cached loci-table row; queried once at init(),
};
