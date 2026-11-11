#include "event.hpp"

#include <algorithm>
#include <cstdint>

#include "cmd.hpp"
#include "input.hpp"
#include "query.hpp"
#include "shared/log.hpp"
#include "state.hpp"
#include "tb2_impl.hpp"
#include "tui_assert.hpp"
#include "widgets.hpp"

namespace g_db = tui_state::db;
namespace g_browsr = tui_state::ui::browsr;
namespace g_cmd = tui_state::ui::cmd;
namespace g_overlay = tui_state::ui::overlay;
namespace g_ui = tui_state::ui;

void handle_event (const tb_event* ev)
{
  using enum tui_state::focus_mode_t;
  /* called before drawing each frame */

  apb_log::log_push (
      fmt::format (
          "Recieved event: .type: {}, .key: {}, .ch: {}, .mod: {}, .w: "
          "{}, .h: {}\n\tinput buffer: {}",
          ev->type, ev->key, ev->ch, ev->mod, ev->w, ev->h,
          apb_read_tb2_global_in()
      )
  );

  if (ev->type == TB_EVENT_RESIZE) {
    g_ui::tooSmall = !size_main_widgets();
    if (tui_state::focus == overlayMode &&
        !size_and_set_overlay_widget (g_overlay::content)) {
      tui_state::focus = promptMode;
      g_cmd::msgBuf =
          "Closed overlay - terminal too small to display pane";
    }
    return;
  }

  /* non-modal key bindings */

  tb_event trailing;
  switch (ev->key) {
  case TB_KEY_ESC:
    // peek "pops" event out of buffer, despite the name
    if (tb_peek_event (&trailing, 0) == TB_OK &&
        trailing.type == TB_EVENT_KEY) {
      apb_log::log_push ("Detected alt modifier, modifying key event");
      trailing.mod |= TB_MOD_ALT;
      ev = &trailing;
      break;
    }
    else if (tui_state::focus == browserMode) {
      apb_log::log_push ("focusing prompt from browser");
      tui_state::focus = promptMode;
      return;
    }
    else if (tui_state::focus == promptMode) {
      apb_log::log_push ("focusing browser from prompt");
      tui_state::focus = browserMode;
      return;
    }
    else if (tui_state::focus == overlayMode) {
      apb_log::log_push ("focusing prompt from overlay");
      g_overlay::contentLnOffset = 0;
      tui_state::focus = promptMode;
      return;
    }

  case TB_KEY_CTRL_C:
    tui_state::run = false;
    return;
  default:
    break;
  }

  /* modal bindings */
  // FIXME: should prompt be active during overlay mode?
  // It would let you type looking at help. But then help must
  // not overlap overlay. In future at least, I think yes.
  switch (tui_state::focus) {
  case browserMode: {
    constexpr auto sideScrollIncrement = 3;
    auto& stmtRowScrollOffset = g_db::stmtRowScrollOffset;

    switch (ev->key) {
    case TB_KEY_ARROW_LEFT:
      // side scroll aln pane
      g_browsr::userPanOffset -= sideScrollIncrement;
      return;
    case TB_KEY_ARROW_RIGHT:
      g_browsr::userPanOffset += sideScrollIncrement;
      return;

    case TB_KEY_ARROW_DOWN: {
      const auto lastRowOnscreen = static_cast<uint32_t> (
          stmtRowScrollOffset + g_browsr::nUnitsOnscreen
      );
      if (lastRowOnscreen < g_db::nScrollRows()) {
        stmtRowScrollOffset++;
      }
      return;
    }

    case TB_KEY_ARROW_UP:
      stmtRowScrollOffset = std::max (stmtRowScrollOffset - 1, 0);
      return;

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
      return;
    }

    case TB_KEY_PGDN: {
      const auto lastRowOnscreen = static_cast<uint32_t> (
          stmtRowScrollOffset + g_browsr::nUnitsOnscreen
      );
      if (lastRowOnscreen < g_db::nScrollRows()) {
        stmtRowScrollOffset += g_browsr::nUnitsOnscreen;
      }
      return;
    }

    default:
      return;
    }
  }
  case promptMode: {
    if (ev->key == 0 && ev->ch != 0 && ev->mod == 0) {
      // Unmodified character input (shift is not reported as a modifier for uppercase letters)
      input::insert (g_cmd::cmdBuf.buf, static_cast<char> (ev->ch));
    }

    if ((ev->mod & TB_MOD_ALT) != 0 && ev->ch == 'b') {
      input::move_word_left (g_cmd::cmdBuf.buf);
      return;
    }
    else if ((ev->mod & TB_MOD_ALT) != 0 && ev->ch == 'f') {
      input::move_word_right (g_cmd::cmdBuf.buf);
    }
    else if ((ev->mod & TB_MOD_ALT) != 0 && ev->ch == 'd') {
      // equivalent to alt-del
      input::del_word_right (g_cmd::cmdBuf.buf);
    }

    switch (ev->key) {
    case TB_KEY_ENTER:
      // execute user command
      if (!g_cmd::cmdBuf.buf.text.empty()) {
        auto cmdResult = exec_cmd (g_cmd::cmdBuf.buf.text);
        switch (cmdResult.first) {
        case ExecStatus::success:
          input::reset (g_cmd::cmdBuf);
        case ExecStatus::fail:
        case ExecStatus::unknown:
          break;
        }
        // return msg
        g_cmd::msgBuf = cmdResult.second;
      }
      return;

    case TB_KEY_BACKSPACE:
    case TB_KEY_BACKSPACE2:
      if ((ev->mod & TB_MOD_ALT) != 0) {
        input::del_word_left (g_cmd::cmdBuf.buf);
      }
      else {
        input::del_back (g_cmd::cmdBuf.buf);
      }
      return;

    case TB_KEY_DELETE:
    case TB_KEY_CTRL_D:
      input::del_fwd (g_cmd::cmdBuf.buf);
      return;

    case TB_KEY_ARROW_LEFT:
      if ((ev->mod & (TB_MOD_CTRL | TB_MOD_ALT)) != 0) {
        // NOTE: alt-left overlaps alt-b
        input::move_word_left (g_cmd::cmdBuf.buf);
      }
      else {
        input::move_left (g_cmd::cmdBuf.buf);
      }
      return;

    case TB_KEY_ARROW_RIGHT:
      if ((ev->mod & (TB_MOD_CTRL | TB_MOD_ALT)) != 0) {
        // NOTE: alt-right overlaps alt-f
        input::move_word_right (g_cmd::cmdBuf.buf);
      }
      else {
        input::move_right (g_cmd::cmdBuf.buf);
      }
      return;

    case TB_KEY_CTRL_A:
    case TB_KEY_HOME:
      input::move_start (g_cmd::cmdBuf.buf);
      return;

    case TB_KEY_CTRL_E:
    case TB_KEY_END:
      input::move_end (g_cmd::cmdBuf.buf);
      return;

    case TB_KEY_CTRL_K:
      input::del_to_end (g_cmd::cmdBuf.buf);
      return;

    case TB_KEY_CTRL_U:
      input::del_to_start (g_cmd::cmdBuf.buf);
      return;

    case TB_KEY_ARROW_UP:
      input::history_prev (g_cmd::cmdBuf);
      return;

    case TB_KEY_ARROW_DOWN:
      input::history_next (g_cmd::cmdBuf);
      return;

      /* --- query management --- */
    case TB_KEY_CTRL_W: {
      const auto& query = g_db::userQuery;
      if (!query.where.empty()) {
        std::string recall;
        recall.append ("WHERE ");
        recall.append (query.where);
        input::set_text (g_cmd::cmdBuf.buf, recall);
        input::move_end (g_cmd::cmdBuf.buf);
      }
      return;
    }
    case TB_KEY_CTRL_O: {
      const auto& query = g_db::userQuery;
      if (!query.orderBy.empty()) {
        std::string recall;
        recall.append ("ORDER BY ");
        recall.append (query.orderBy);
        input::set_text (g_cmd::cmdBuf.buf, recall);
        input::move_end (g_cmd::cmdBuf.buf);
      }
      return;
    }
    case TB_KEY_CTRL_SLASH: {
      auto& undoStack = g_db::queryUndoStack;
      if (undoStack.empty()) {
        return;
      }
      auto query = undoStack.back();
      undoStack.pop_back();
      if (const auto ret = g_db::set_query (query); ret != SQLITE_OK) {
        TUI_UNREACHABLE (
            "SQL in undo stack failed to compile upon reuse!"
        );
      }
      // set_query always adds to undo queue on success,
      // so reverse that.
      undoStack.pop_back();
      return;
    }

    default:
      return;
    }
  }
  case overlayMode: {
    if (ev->key != 0) {
      auto& lnOff = g_overlay::contentLnOffset;
      const auto contentLines =
          static_cast<int> (g_overlay::content.size());
      auto maxScroll =
          std::max (0, contentLines - height (g_overlay::contentBox));
      switch (ev->key) {
      case TB_KEY_ARROW_DOWN:
        lnOff = std::min (maxScroll, lnOff + 1);
        return;
      case TB_KEY_ARROW_UP:
        lnOff = std::max (0, lnOff - 1);
        return;
      default:
        return;
      }
    }
  }
  }
}
