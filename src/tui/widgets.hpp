#pragma once

#include <sqlite3.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "extb/box/box.hpp"
#include "extb/extb.hpp"
#include "helpblocks.hpp"
#include "history.hpp"
#include "input.hpp"

struct ColMetadata {
  bool visible;
  const std::string_view fieldName;
  const uint16_t displayWidth;
  std::string (*const fn_retrieve_from_db) (sqlite3_stmt*);
};

namespace tui_state::ui {

extern std::array<ColMetadata, 10> tableCols;

inline constinit bool showOverlay = false;
// last resize left terminal too small; widget geometry is stale
inline constinit bool tooSmall = false;

// user toggles
inline constinit bool drawQualTrack = false;
inline constinit bool drawInsTrack = true;
inline constinit bool drawTable = true;
inline constinit bool drawSparkline = false;

inline constexpr auto kMinWidth = 20;

namespace browsr {

// Can't meaningfully display data below this height
inline constexpr auto kMinWidgetHeight = 8;

inline constinit extb::Box frame;
inline constinit extb::HLine alnPaneRefLine;
inline constinit extb::HLine tablePaneHeaderLine;
inline constinit extb::HLine headerSep;
inline constinit extb::Box alnPaneDataBox;
inline constinit extb::VLine vSep;
inline constinit extb::Box tablePaneDataBox;
// scroll units: reads + empty-file labels
inline constinit uint16_t nUnitsOnscreen = 0;
inline constinit int64_t userPanOffset = 0;

}  // namespace browsr

namespace cmd {

// cmd widget has fixed height. browser widget
// scales to accomodate.
inline constexpr auto kWidgetHeight = 3;  // inc. borders

inline constinit extb::Box frame;
inline constinit extb::HLine contextLine;
// inline constinit extb::HLine
//     statusLine;  // for displaying current filter applied to records
inline constinit extb::HLine inputSep;
inline constinit extb::HLine inputLine;
inline constinit extb::GlobalCell inputCaret;
inline constinit EditBuf inputBuf;
inline constinit CmdHistory inputHistory;
inline constinit extb::HLine msgLine;
inline constinit std::string msgBuf;

}  // namespace cmd

namespace overlay {

inline constexpr auto kMinWidgetWidth = 10;
inline constexpr auto kMinWidgetHeight = 10;

inline constinit extb::Box frame;
inline constinit extb::Box contentBox;
inline constinit helpblocks::TextBlockRef content = helpblocks::app;
inline constinit int contentLnOffset = 0;

}  // namespace overlay

}  // namespace tui_state::ui

// Calculates and sets sizes on statically-sized UI elements.
// Returns true on success, false on failure due
// to insufficient space available.
[[nodiscard]] bool size_main_widgets();
// Calculates and sets sizes on dynamically-sized overlay widget.
// Returns true on success, false on failure due
// to insufficient space available.
[[nodiscard]] bool size_and_set_overlay_widget (
    helpblocks::TextBlockRef content
);

void draw_main_to_buffer();
// draw (sized and set) overlay widget.
// asserts required preconditions, otherwise
// should not fail, so void return.
void draw_overlay_to_buffer();
