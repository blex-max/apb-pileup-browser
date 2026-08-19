#pragma once

#include "app/data_table_cols.hpp"
#include "app/state_components.hpp"
#include "app/text_blocks.hpp"
#include "frontend/extb/box/box.hpp"
#include "frontend/history.hpp"
#include "frontend/input.hpp"
#include "shared/err.hpp"

namespace e2 = extb;

struct BrowserWgt {
  e2::Box frame;
  e2::HLine refLine;
  e2::HLine headerLine;
  e2::HLine headerSep;
  e2::Box queryBox;
  e2::VLine vSep;
  e2::Box dataBox;
  e2::HLine querySep;
  e2::HLine infoLine;
  int rowStart = 0; // TODO move?
};
struct CmdWgt {
  e2::Box frame;
  e2::HLine
      queryStatusLine;  // for displaying current filter applied to records
  e2::HLine statusSep;
  e2::HLine inputLine;
  e2::GlobalCell inputCaret;
  EditBuf inputBuf;  // namespace?
  CmdHistory history;
  e2::HLine sepLine;
  e2::HLine msgLine;
  std::string msgBuf;
};
static constexpr auto sh_cmdH = 7;  // inc. borders

struct OverlayWgt {
  e2::Box frame;
  e2::Box contentBox;
  std::span<const std::string_view> content =
      get_text_block (TxtBlockId::generalHelp);
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


void set_overlay_widget (UIBundle& ui, TextBlockRef content);
void draw_overlay (const OverlayWgt& oWgt);

void size_browser_panes (BrowserWgt& bWgt, double seqPaneFrac);
VoidOrErr size_widgets (UIBundle& ui, double seqPaneFrac);

VoidOrErr draw_main_ui (
    UIBundle& ui, DBBundle& db, const DataColList& colsRequested
);
