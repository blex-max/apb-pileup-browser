#pragma once

#include <list>

#include "app/data_table_cols.hpp"
#include "backend/PileupDB.hpp"
#include "frontend/extb/box/box.hpp"
#include "frontend/extb/extb.hpp"
#include "frontend/history.hpp"
#include "frontend/input.hpp"

namespace e2 = extb;

// TODO: separate Frame widget type?
struct PileupWgt {
  e2::Box frame;
  e2::JLine refLine;
  e2::JLine headerLine;
  e2::JLine headerSep;
  e2::Box queryBox;
  e2::ILine vSep;
  e2::Box dataBox;
  e2::JLine querySep;
  e2::JLine infoLine;
  int rowStart = 0; // TODO move?
};
struct CmdWgt {
  e2::Box frame;
  e2::JLine
      queryStatusLine;  // for displaying current filter applied to records
  e2::JLine statusSep;
  e2::JLine inputLine;
  e2::GlobalCell inputCaret;
  EditBuf inputBuf;  // namespace?
  CmdHistory history;
  e2::JLine sepLine;
  e2::JLine msgLine;
  std::string msgBuf;
};
static constexpr auto CMD_H = 7;  // inc. borders

struct OverlayWgt {
  e2::Box frame;
  e2::Box contentBox;
  std::string_view content;
};

struct TopUI {
  PileupWgt main;
  CmdWgt cmd;
  OverlayWgt help;
};

struct AppConfig {
  bool run = true;
  std::list<const DataTableCol*> dataColsRequested{
      &cols::basequal, &cols::rstart, &cols::rend, &cols::flag,
      &cols::mapq,     &cols::cigar,  &cols::qname
  };  // preseves insertion order, allows removal by val
  double query_box_frac = 0.5;
  bool showOverlay = false;
};

struct Metadata {
  size_t c_frame = 0;  // c_ == counter
  tb_event last_ev{};
};

struct AppState {
  TopUI ui;
  AppConfig conf;
  Metadata mData;
  PileupDB db;
  LocusData
      locus;  // cached loci-table row; queried once at init(),
  // not re-queried per frame.
  struct {
    DynamicSelectReadsStmt stmt;
    DynamicFragments userClause{};
  } query;
};
