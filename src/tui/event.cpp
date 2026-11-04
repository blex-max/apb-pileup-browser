#include "event.hpp"

#include <algorithm>
#include <cstdint>

#include "cmd.hpp"
#include "input.hpp"
#include "query.hpp"
#include "state.hpp"
#include "tui/tui_assert.hpp"
#include "widgets.hpp"

namespace g_db = tui_state::db;
namespace g_browsr = tui_state::ui::browsr;
namespace g_cmd = tui_state::ui::cmd;
namespace g_overlay = tui_state::ui::overlay;
namespace g_ui = tui_state::ui;

static bool handle_nav (const tb_event& ev)
{
  constexpr auto sideScrollIncrement = 3;

  auto& stmtRowScrollOffset = g_db::stmtRowScrollOffset;

  switch (ev.key) {
    case TB_KEY_ENTER:
      // execute user command
      if (!g_cmd::cmdBuf.buf.text.empty()) {
        g_cmd::msgBuf =
            exec_cmd (input::reset (g_cmd::cmdBuf));  // return msg
      }
      break;

    case TB_KEY_BACKSPACE:
    case TB_KEY_BACKSPACE2:
      if ((ev.mod & TB_MOD_ALT) != 0) {
        input::del_word_left (g_cmd::cmdBuf.buf);
      }
      else {
        input::del_back (g_cmd::cmdBuf.buf);
      }
      break;

    case TB_KEY_DELETE:
      if (ev.mod == 0) {
        input::del_fwd (g_cmd::cmdBuf.buf);
      }
      break;

    case TB_KEY_ARROW_LEFT:
      if ((ev.mod & TB_MOD_SHIFT) != 0) {
        // side scroll aln pane
        g_browsr::userPanOffset -= sideScrollIncrement;
      }
      else if ((ev.mod & (TB_MOD_CTRL | TB_MOD_ALT)) != 0) {
        input::move_word_left (g_cmd::cmdBuf.buf);
      }
      else {
        input::move_left (g_cmd::cmdBuf.buf);
      }
      break;

    case TB_KEY_ARROW_RIGHT:
      if ((ev.mod & TB_MOD_SHIFT) != 0) {
        g_browsr::userPanOffset += sideScrollIncrement;
      }
      else if ((ev.mod & (TB_MOD_CTRL | TB_MOD_ALT)) != 0) {
        input::move_word_right (g_cmd::cmdBuf.buf);
      }
      else {
        input::move_right (g_cmd::cmdBuf.buf);
      }
      break;

    case TB_KEY_CTRL_A:
    case TB_KEY_HOME:
      input::move_start (g_cmd::cmdBuf.buf);
      break;

    case TB_KEY_CTRL_C:
      if (!g_cmd::cmdBuf.buf.text.empty()) {
        input::clear (g_cmd::cmdBuf.buf);
      }
      else {
        tui_state::run = false;
      }
      break;

    case TB_KEY_CTRL_E:
    case TB_KEY_END:
      input::move_end (g_cmd::cmdBuf.buf);
      break;

    case TB_KEY_CTRL_K:
      input::del_to_end (g_cmd::cmdBuf.buf);
      break;

    case TB_KEY_CTRL_U:
      input::del_to_start (g_cmd::cmdBuf.buf);
      break;

    case TB_KEY_CTRL_P:
      input::history_prev (g_cmd::cmdBuf);
      break;

    case TB_KEY_CTRL_N:
      input::history_next (g_cmd::cmdBuf);
      break;

    case TB_KEY_ARROW_DOWN: {
      const auto lastRowOnscreen = static_cast<uint32_t> (
          stmtRowScrollOffset + g_browsr::nUnitsOnscreen
      );
      if (lastRowOnscreen < g_db::nScrollRows()) {
        stmtRowScrollOffset++;
      }
    } break;

    case TB_KEY_ARROW_UP:
      stmtRowScrollOffset = std::max (stmtRowScrollOffset - 1, 0);
      break;

    case TB_KEY_PGUP: {
      // since number of tracks is dynamic both by setting
      // and onscreen content, must derive safe number
      // of rows to scroll up.
      auto rowsPerRead = [&]() {
        auto rowsInUse = 1 + static_cast<int> (g_ui::drawInsTrack) +
                         static_cast<int> (g_ui::drawQualTrack);
        if (g_ui::drawInsTrack && g_ui::drawQualTrack) {
          rowsInUse += 1;
        }
        if (g_db::locusInfo.nAln > 1) {
          rowsInUse += 1;  // file header, worst case one per read
        }
        return rowsInUse;
      }();
      const auto minReadsPerPage =
          height (g_browsr::alnPaneDataBox) / rowsPerRead;
      stmtRowScrollOffset =
          std::max (stmtRowScrollOffset - minReadsPerPage, 0);
      break;
    }

    case TB_KEY_PGDN: {
      const auto lastRowOnscreen = static_cast<uint32_t> (
          stmtRowScrollOffset + g_browsr::nUnitsOnscreen
      );
      if (lastRowOnscreen < g_db::nScrollRows()) {
        stmtRowScrollOffset += g_browsr::nUnitsOnscreen;
      }
      break;
    }

    /* --- query management --- */
    case TB_KEY_CTRL_Q: {
      std::string recall;
      const auto& query = g_db::userQuery;
      if (!query.where.empty()) {
        recall.append ("WHERE ");
        recall.append (query.where);
      }
      if (!query.orderBy.empty()) {
        if (!recall.empty()) {
          recall.append (" ");
        }
        recall.append ("ORDER BY ");
        recall.append (query.orderBy);
      }
      input::set_text (g_cmd::cmdBuf.buf, recall);
      input::move_end (g_cmd::cmdBuf.buf);
    } break;
    case TB_KEY_CTRL_W: {
      const auto& query = g_db::userQuery;
      if (!query.where.empty()) {
        std::string recall;
        recall.append ("WHERE ");
        recall.append (query.where);
        input::set_text (g_cmd::cmdBuf.buf, recall);
        input::move_end (g_cmd::cmdBuf.buf);
      }
    } break;
    case TB_KEY_CTRL_O: {
      const auto& query = g_db::userQuery;
      if (!query.orderBy.empty()) {
        std::string recall;
        recall.append ("ORDER BY ");
        recall.append (query.orderBy);
        input::set_text (g_cmd::cmdBuf.buf, recall);
        input::move_end (g_cmd::cmdBuf.buf);
      }
    } break;
    case TB_KEY_CTRL_SLASH: {
      auto& undoStack = g_db::queryUndoStack;
      if (undoStack.empty()) {
        break;
      }
      auto query = undoStack.back();
      undoStack.pop_back();
      if (const auto ret = g_db::set_query (query);
          ret != tui_state::db::SetQueryStatus::success) {
        TUI_UNREACHABLE (
            "SQL in undo stack failed to compile upon reuse!"
        );
      }
      // set_query always adds to undo queue on success,
      // so reverse that.
      undoStack.pop_back();
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
      input::move_word_left (g_cmd::cmdBuf.buf);
    }
    else if ((ev.mod & TB_MOD_ALT) != 0 && ev.ch == 'f') {
      input::move_word_right (g_cmd::cmdBuf.buf);
    }
    else if ((ev.mod & TB_MOD_ALT) != 0 && ev.ch == 'd') {
      input::del_word_right (g_cmd::cmdBuf.buf);
    }
    else {
      input::insert (g_cmd::cmdBuf.buf, static_cast<char> (ev.ch));
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

void handle_event (const tb_event& ev)
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
    g_ui::tooSmall = !size_main_widgets();
    if (g_ui::showOverlay &&
        !size_and_set_overlay_widget (g_overlay::content)) {
      g_ui::showOverlay = false;
      g_cmd::msgBuf = "Failed - terminal too small to display help pane";
    }
  }
}
