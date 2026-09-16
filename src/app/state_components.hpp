#pragma once

#include "app/data_table_cols.hpp"
#include "backend/PileupDB.hpp"
#include "frontend/extb/extb.hpp"


struct AppConfig {
  bool run = true;

  std::array<std::pair<bool, TableCol::ColMetadata*>, 11>
      displayTableCols{
          {{false, &TableCol::sh_colQPos},
           {true, &TableCol::sh_colBaseQual},
           {true, &TableCol::sh_colFlag},
           {true, &TableCol::sh_colMapq},
           {false, &TableCol::sh_colRstart},
           {false, &TableCol::sh_colRend},
           {true, &TableCol::sh_colCigar},
           {false, &TableCol::sh_colQname},
           {false, &TableCol::sh_colMTid},
           {false, &TableCol::sh_colMStart},
           {false, &TableCol::sh_colTags}}
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
