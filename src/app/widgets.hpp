#pragma once

#include "app/helpblocks.hpp"
#include "app/state_components.hpp"
#include "frontend/extb/box/box.hpp"
#include "frontend/history.hpp"
#include "frontend/input.hpp"
#include "shared/err.hpp"

namespace e2 = extb;

struct BrowserWgt {
  e2::Box frame;
  e2::HLine alnPaneRefLine;
  e2::HLine tablePaneHeaderLine;
  e2::HLine headerSep;
  e2::Box alnPaneDataBox;
  e2::VLine vSep;
  e2::Box tablePaneDataBox;
  e2::HLine ambientSep;
  e2::HLine ambientLine;
  uint16_t nReadOnscreen = 0;
  int64_t userPanOffset = 0;
};
struct CmdWgt {
  static constexpr auto widgetHeight = 7;  // inc. borders
  e2::Box frame;
  e2::HLine
      queryStatusLine;  // for displaying current filter applied to records
  e2::HLine statusSep;
  e2::HLine inputLine;
  e2::GlobalCell inputCaret;
  EditBuf inputBuf;
  CmdHistory history;
  e2::HLine sepLine;
  e2::HLine msgLine;
  std::string msgBuf;
};

struct OverlayWgt {
  e2::Box frame;
  e2::Box contentBox;
  helpblocks::TextBlockRef content = helpblocks::app;
  int contentLnOffset = 0;
};

struct UIBundle {
  BrowserWgt browsr;
  CmdWgt cmd;
  OverlayWgt help;
  int screenH = -1;
  int screenW = -1;
  // TODO: add individual dirty flags
  // for each widget
};

VoidOrErr size_widgets (UIBundle& ui);
void size_and_set_overlay_widget (
    UIBundle& ui, helpblocks::TextBlockRef content
);

VoidOrErr draw_main_ui (
    UIBundle& ui, DBBundle& db, const AppConfig& conf
);
void draw_overlay (const OverlayWgt& oWgt);
