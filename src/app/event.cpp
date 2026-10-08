#include "app/event.hpp"

#include <fmt/format.h>

#include "app/cmd.hpp"
#include "app/g_state.hpp"
#include "app/widgets.hpp"
#include "frontend/extb/extb.hpp"
#include "frontend/history.hpp"
#include "frontend/input.hpp"

namespace g_db = g_state::db;
namespace g_browsr = g_state::ui::browsr;
namespace g_cmd = g_state::ui::cmd;
namespace g_overlay = g_state::ui::overlay;
namespace g_ui = g_state::ui;

static bool handle_nav (const tb_event& ev)
{
  constexpr auto sideScrollIncrement = 3;

  auto& stmtRowScrollOffset = g_db::stmtRowScrollOffset;

  switch (ev.key) {
    case TB_KEY_ENTER:
      // execute user command
      if (!g_cmd::inputBuf.text.empty()) {
        history_push (g_cmd::history, g_cmd::inputBuf.text);
        g_cmd::msgBuf = exec_cmd (g_cmd::inputBuf.text).msg;  // return msg
        clear (g_cmd::inputBuf);
      }
      break;

    case TB_KEY_BACKSPACE:
    case TB_KEY_BACKSPACE2:
      if ((ev.mod & TB_MOD_ALT) != 0) {
        clear (g_cmd::inputBuf);
      }
      else {
        del_back (g_cmd::inputBuf);
      }
      break;

    case TB_KEY_ARROW_LEFT:
      if ((ev.mod & TB_MOD_SHIFT) != 0) {
        // side scroll aln pane
        g_browsr::userPanOffset -= sideScrollIncrement;
      }
      else {
        move_left (g_cmd::inputBuf);
      }
      break;

    case TB_KEY_ARROW_RIGHT:
      if ((ev.mod & TB_MOD_SHIFT) != 0) {
        g_browsr::userPanOffset += sideScrollIncrement;
      }
      else {
        move_right (g_cmd::inputBuf);
      }
      break;

    case TB_KEY_CTRL_A:
      move_start (g_cmd::inputBuf);
      break;

    case TB_KEY_CTRL_C:
      if (!g_cmd::inputBuf.text.empty()) {
        clear (g_cmd::inputBuf);
      }
      else {
        g_state::run = false;
      }
      break;

    case TB_KEY_CTRL_E:
      move_end (g_cmd::inputBuf);
      break;

    case TB_KEY_ARROW_DOWN:
      if ((ev.mod & TB_MOD_SHIFT) != 0) {
        history_next (g_cmd::history, g_cmd::inputBuf);
      }
      else {
        const auto lastRowOnscreen = static_cast<uint32_t> (
            stmtRowScrollOffset + g_browsr::nReadOnscreen
        );
        if (lastRowOnscreen < g_db::nStmtRows) {
          stmtRowScrollOffset++;
        }
      }
      break;

    case TB_KEY_ARROW_UP:
      if ((ev.mod & TB_MOD_SHIFT) != 0) {
        history_prev (g_cmd::history, g_cmd::inputBuf);
      }
      else {
        stmtRowScrollOffset = std::max (stmtRowScrollOffset - 1, 0);
      }
      break;

    case TB_KEY_PGUP: {
      // since number of tracks is dynamic both by setting
      // and onscreen content, must derive safe number
      // of rows to scroll up
      const auto& trackSwitches = g_ui::drawTrackSwitches;
      const auto minReadsPerPage =
          height (g_browsr::alnPaneDataBox) /
          (1 + static_cast<int> (trackSwitches.qual) +
           static_cast<int> (trackSwitches.ins));
      stmtRowScrollOffset =
          std::max (stmtRowScrollOffset - minReadsPerPage, 0);
      break;
    }

    case TB_KEY_PGDN: {
      const auto lastRowOnscreen = static_cast<uint32_t> (
          stmtRowScrollOffset + g_browsr::nReadOnscreen
      );
      if (lastRowOnscreen < g_db::nStmtRows) {
        stmtRowScrollOffset += g_browsr::nReadOnscreen;
      }
      break;
    }

    default:
      return false;
  }

  return true;
}

static void handle_key_event (const tb_event& ev)
{
  if (ev.key == 0 && ev.ch != 0) {
    // annoyingly, outside of handle_nav
    if ((ev.mod & TB_MOD_ALT) != 0 && ev.ch == 'b') {
      move_word_left (g_cmd::inputBuf);
    }
    else if ((ev.mod & TB_MOD_ALT) != 0 && ev.ch == 'f') {
      move_word_right (g_cmd::inputBuf);
    }
    else {
      insert (g_cmd::inputBuf, static_cast<char> (ev.ch));
    }
  }
  else {
    handle_nav (ev);
  }
}

static void nav_overlay (const tb_event& ev)
{
  if (ev.ch != 0) {
    if (ev.ch == 'q') {
      g_ui::showOverlay = false;
      g_overlay::contentLnOffset = 0;
    }
  }
  else if (ev.key != 0) {
    auto& lnOff = g_overlay::contentLnOffset;
    const auto contentLines = static_cast<int> (g_overlay::content.size());
    auto maxScroll =
        std::max (0, contentLines - height (g_overlay::contentBox));
    switch (ev.key) {
      case TB_KEY_ARROW_DOWN:
        lnOff = std::min (maxScroll, lnOff + 1);
        break;
      case TB_KEY_ARROW_UP:
        lnOff = std::max (0, lnOff - 1);
        break;
      default:
        break;
    }
  }
}

WidgetStatus handle_event (const tb_event& ev)
{
  if (ev.type == TB_EVENT_KEY) {
    if (!g_ui::showOverlay) {
      handle_key_event (ev);
    }
    else {
      // overlay nav
      nav_overlay (ev);
    }
  }
  else if (ev.type == TB_EVENT_RESIZE) {
    if (!size_widgets()) {
      return WidgetStatus{WidgetStatus::insufficientSz};
    }
  }

  return {.code = WidgetStatus::success};
}
