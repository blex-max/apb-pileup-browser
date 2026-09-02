#include "app/event.hpp"

#include <fmt/format.h>

#include "app/cmd.hpp"
#include "app/widgets.hpp"
#include "plog/Log.h"

static VoidOrErr handle_resize (UIBundle& ui, double seqPaneFrac)
{
  auto calcRet = size_widgets (ui, seqPaneFrac);
  if (!calcRet) {
    return std::unexpected (calcRet.error());
  }

  return {};
}

static void handle_character_entry (
    AppState& state, const tb_event& ev
)
{
  insert (state.ui.cmd.inputBuf, static_cast<char> (ev.ch));
}

static bool handle_nav (AppState& state, const tb_event& ev)
{
  auto& bWgt = state.ui.browsr;
  auto& cWgt = state.ui.cmd;
  const auto& db = state.db;
  auto& stmtRowScrollOffset = state.db.stmtRowScrollOffset;

  switch (ev.key) {
    case TB_KEY_ENTER:
      // execute user command
      if (!cWgt.inputBuf.text.empty()) {
        history_push (cWgt.history, cWgt.inputBuf.text);
        cWgt.msgBuf = exec_cmd (cWgt.inputBuf.text, state)
                          .msg;  // return msg
        clear (cWgt.inputBuf);
      }
      break;

    case TB_KEY_BACKSPACE:
    case TB_KEY_BACKSPACE2:
      if ((ev.mod & TB_MOD_ALT) != 0) {
        clear (cWgt.inputBuf);
      }
      else {
        del_back (cWgt.inputBuf);
      }
      break;

    case TB_KEY_ARROW_LEFT:
      move_left (cWgt.inputBuf);
      break;

    case TB_KEY_ARROW_RIGHT:
      move_right (cWgt.inputBuf);
      break;

    case TB_KEY_CTRL_A:
      move_start (cWgt.inputBuf);
      break;

    case TB_KEY_CTRL_C:
      if (!cWgt.inputBuf.text.empty()) {
        clear (cWgt.inputBuf);
      }
      else {
        state.conf.run = false;
      }
      break;

    case TB_KEY_CTRL_E:
      move_end (cWgt.inputBuf);
      break;

    case TB_KEY_ARROW_DOWN:
      if ((ev.mod & TB_MOD_SHIFT) != 0) {
        history_next (cWgt.history, cWgt.inputBuf);
      }
      else {
        const auto lastRowOnscreen = static_cast<uint32_t> (
            stmtRowScrollOffset + bWgt.nReadOnscreen
        );
        if (lastRowOnscreen < db.nStmtRows) {
          stmtRowScrollOffset++;
        }
      }
      break;

    case TB_KEY_ARROW_UP:
      if ((ev.mod & TB_MOD_SHIFT) != 0) {
        history_prev (cWgt.history, cWgt.inputBuf);
      }
      else {
        stmtRowScrollOffset =
            std::max (stmtRowScrollOffset - 1, 0);
      }
      break;

    case TB_KEY_PGUP: {
      // since number of tracks is dynamic both by setting
      // and onscreen content, must derive safe number
      // of rows to scroll up
      const auto minReadsPerPage =
          height (bWgt.seqPane) /
          (1 + static_cast<int> (state.conf.drawQualTrack) +
           static_cast<int> (state.conf.drawInsTrack) +
           static_cast<int> (state.conf.drawInsQualTrack));
      stmtRowScrollOffset =
          std::max (stmtRowScrollOffset - minReadsPerPage, 0);
      break;
    }

    case TB_KEY_PGDN: {
      const auto lastRowOnscreen = static_cast<uint32_t> (
          stmtRowScrollOffset + bWgt.nReadOnscreen
      );
      if (lastRowOnscreen < db.nStmtRows) {
        stmtRowScrollOffset += bWgt.nReadOnscreen;
      }
      break;
    }

    default:
      return false;
  }

  return true;
}

static void handle_key_event (
    AppState& state, const tb_event& ev
)
{
  if (ev.key == 0 && ev.ch != 0) {
    // annoyingly, outside of handle_nav
    if ((ev.mod & TB_MOD_ALT) != 0 && ev.ch == 'b') {
      PLOGD << "Recieved alt-b (word-left) event";
      move_word_left (state.ui.cmd.inputBuf);
    }
    else if ((ev.mod & TB_MOD_ALT) != 0 && ev.ch == 'f') {
      PLOGD << "Recieved alt-f (word-right) event";
      move_word_right (state.ui.cmd.inputBuf);
    }
    else {
      PLOGD << fmt::format (
          "Recieved character input event: {}", ev.ch
      );
      handle_character_entry (state, ev);
    }
  }
  else {
    PLOGD << fmt::format (
        "Recieved navigation event: {}", ev.key
    );
    handle_nav (state, ev);
  }
}

static void nav_overlay (AppState& state, const tb_event& ev)
{
  if (ev.ch != 0) {
    if (ev.ch == 'q') {
      state.conf.showOverlay = false;
      state.ui.help.contentLnOffset = 0;
    }
  }
  else if (ev.key != 0) {
    auto& lnOff = state.ui.help.contentLnOffset;
    const auto contentLines =
        static_cast<int> (state.ui.help.content.size());
    auto maxScroll = std::max (
        0, contentLines - height (state.ui.help.contentBox)
    );
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

// Does this need to access the
// whole appstate struct?
// (the only reason to care is sprawl
// and maintainability)
// TODO: probably not
VoidOrErr handle_event (AppState& state, const tb_event& ev)
{
  PLOGD << "Recieved event";
  if (ev.type == TB_EVENT_KEY) {
    if (!state.conf.showOverlay) {
      handle_key_event (state, ev);
    }
    else {
      // overlay nav
      nav_overlay (state, ev);
    }
  }
  else if (ev.type == TB_EVENT_RESIZE) {
    auto rszRet =
        handle_resize (state.ui, state.conf.seqPaneFrac);
    if (!rszRet) {
      return std::unexpected (rszRet.error());
    }
  }

  return {};
}
