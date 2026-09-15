#pragma once

#include "app/data_table_cols.hpp"
#include "backend/PileupDB.hpp"
#include "frontend/extb/extb.hpp"


struct AppConfig {
  bool run = true;

  // TODO: fixed array of all columns + on/off bool
  // N.B> this probably reduces the scope/need for
  // the data_table_cols.*pp machinery!
  std::vector<TableCol::ID> displayTableCols{
      TableCol::ID::basequal, TableCol::ID::rstart,
      TableCol::ID::rend,     TableCol::ID::flag,
      TableCol::ID::mapq,     TableCol::ID::cigar,
      TableCol::ID::qname
  };

  bool showOverlay = false;
  struct {
    bool qual = false;
    bool ins = true;
  } drawTrackSwitches;
  struct {
    bool aln = true;
    bool table = true;
  } drawPaneSwitches;
};

struct AppMetadata {
  size_t frameCount = 0;
  tb_event lastEv{};
};

struct DBBundle {
  PileupDB db;
  DynamicSelectReadsStmt stmt;
  DynamicFragments userClause{};
  uint32_t nStmtRows = 0;  // rows in current stmt
  int32_t stmtRowScrollOffset = 0;
  PileupMetadata
      locus;  // cached loci-table row; queried once at init(),
};
