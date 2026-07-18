#pragma once

#include "frontend/extb/extb.hpp"
#include "frontend/extb/widgets/box.hpp"
#include "frontend/input.hpp"

namespace e2 = extb;

// TODO: separate Frame widget type?
struct PileupWidg {
  e2::Box frame;
  e2::JLine refLine;
  e2::Box queryBox;
  e2::JLine statusLine;
  e2::Box dataBox;
  int rowStart = 0; // TODO move?
};
struct CmdWidg {
  e2::Box frame;
  e2::JLine inputLine;
  e2::GlobalCell inputCaret;
  EditBuf inputBuf;  // namespace?
  e2::JLine sepLine;
  e2::JLine msgLine;
  std::string msgBuf;
};
static constexpr auto CMD_H = 5;  // inc. borders

struct TopUI {
  PileupWidg main;
  CmdWidg cmd;
};

struct AppConfig {
  bool run = true;
  // std::list<PropRequest> bam_props_request;
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
};
