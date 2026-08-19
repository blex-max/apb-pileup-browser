#pragma once

#include "app/data_table_cols.hpp"
#include "backend/PileupDB.hpp"
#include "frontend/extb/extb.hpp"


constexpr double sh_defaultSeqPaneFrac = 0.5;
struct AppConfig {
  bool run = true;
  DataColList colsRequested{
      find_cols (
          {DataColID::basequal, DataColID::rstart,
           DataColID::rend, DataColID::flag, DataColID::mapq,
           DataColID::cigar, DataColID::qname}
      )
  };  // list preseves insertion order, and allows removal by val
  double seqPaneFrac = sh_defaultSeqPaneFrac;
  bool showOverlay = false;
};

struct AppMetadata {
  size_t frameCount = 0;
  tb_event lastEv{};
};

struct DBBundle {
  PileupDB db;
  DynamicSelectReadsStmt stmt;
  DynamicFragments userClause{};
  PileupMetadata
      locus;  // cached loci-table row; queried once at init(),
};
