#pragma once

#include "app/state_components.hpp"
#include "app/text_blocks.hpp"
#include "frontend/extb/box/box.hpp"
#include "frontend/history.hpp"
#include "frontend/input.hpp"
#include "shared/err.hpp"

namespace e2 = extb;

struct BrowserWgt {
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
  std::span<const std::string_view> content =
      get_text_block (TxtBlockId::generalHelp);
  int contentLnOffset = 0;
};

struct UIBundle {
  BrowserWgt main;
  CmdWgt cmd;
  OverlayWgt help;
  int screenH = -1;
  int screenW = -1;
};


void set_overlay_widget (UIBundle& ui, TextBlockRef content);
void draw_overlay (const OverlayWgt& oWgt);

void size_browser_panes (BrowserWgt& pWgt, double seqPaneFrac);
VoidOrErr size_widgets (UIBundle& ui, double seqPaneFrac);

VoidOrErr draw_main_ui (
    UIBundle& ui, DBBundle& db,
    const DataRequestList& colsRequested
);
