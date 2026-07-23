#pragma once

#include <list>

#include "app/fields.hpp"
#include "backend/PileupDB.hpp"
#include "frontend/extb/extb.hpp"
#include "frontend/extb/widgets/box.hpp"
#include "frontend/history.hpp"
#include "frontend/input.hpp"

namespace e2 = extb;

// TODO: separate Frame widget type?
struct PileupWidg {
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
struct CmdWidg {
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

struct TopUI {
  PileupWidg main;
  CmdWidg cmd;
};

struct AppConfig {
  bool run = true;
  std::list<const TableField*> dataFieldsRequested{
      &fields::basequal,
      &fields::rstart,
      &fields::rend,
      &fields::flag,
      &fields::cigar,
      &fields::qname
  };  // preseves insertion order, allows removal by val
  double query_box_frac = 0.4;
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
    DynamicFragments userClause{.offset = 0};
  } query;
};
