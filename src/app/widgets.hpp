#pragma once

#include "app/helpblocks.hpp"
#include "app/state_components.hpp"
#include "frontend/extb/box/box.hpp"
#include "frontend/history.hpp"
#include "frontend/input.hpp"

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
  OverlayWgt overlay;
  int screenH = -1;
  int screenW = -1;
  // TODO: add individual dirty flags
  // for each widget
};

// Calculates and sets sizes on statically-sized UI elements.
// Returns true on success, false on failure due
// to insufficient space available.
[[nodiscard]] bool size_widgets (UIBundle& ui);
// Calculates and sets sizes on dynamically-sized overlay widget.
// Returns true on success, false on failure due
// to insufficient space available.
[[nodiscard]] bool size_and_set_overlay_widget (
    OverlayWgt& oWgt, helpblocks::TextBlockRef content,
    int screenW, int screenH
);

// NOTE: this reasonably well scoped shared err type is working
// quite well so far
// NOTE: also starting to think (optional) output fill err parameters
// are a good idea...
// NOTE: A Zig-like extensible global error union thing might be nice.
// Maybe a shared err namespace with a success state in it? but then I doubt
// you can extend the enum in various parts of the codebase independently...
struct WidgetStatus {
  enum Code : uint8_t {
    success,
    insufficientSz,
  };
  Code code;
};
[[nodiscard]] WidgetStatus draw_main_ui (
    UIBundle& ui, DBBundle& db, const AppConfig& conf
);
// draw (sized and set) overlay widget.
// asserts required preconditions, otherwise
// should not fail, so void return.
void draw_overlay (const OverlayWgt& oWgt);
