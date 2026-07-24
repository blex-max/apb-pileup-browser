#pragma once

#include "app/cmd.hpp"
#include "app/state.hpp"
#include "app/widgets.hpp"
#include "frontend/extb/extb.hpp"
#include "shared/err.hpp"

inline VoidOrErr handle_resize (AppState& state)
{
  calc_static_widgets (state.ui, state.conf);

  return {};
}

inline void handle_character_entry (
    AppState& state, const tb_event& ev
)
{
  insert (state.ui.cmd.inputBuf, static_cast<char> (ev.ch));
}

inline bool handle_nav (AppState& state, const tb_event& ev)
{
  auto& cmdWgt = state.ui.cmd;
  auto& scrollRow = state.ui.main.rowStart;

  switch (ev.key) {
    case TB_KEY_ENTER:
      // execute user command
      if (!cmdWgt.inputBuf.text.empty()) {
        history_push (cmdWgt.history, cmdWgt.inputBuf.text);
        cmdWgt.msgBuf = exec_cmd (cmdWgt.inputBuf.text, state)
                            .msg;  // return msg
        clear (cmdWgt.inputBuf);
      }
      break;

    case TB_KEY_BACKSPACE:
    case TB_KEY_BACKSPACE2:
      if (ev.mod & TB_MOD_ALT) {
        clear (cmdWgt.inputBuf);
      }
      else {
        del_back (cmdWgt.inputBuf);
      }
      break;

    case TB_KEY_ARROW_LEFT:
      move_left (cmdWgt.inputBuf);
      break;

    case TB_KEY_ARROW_RIGHT:
      move_right (cmdWgt.inputBuf);
      break;

    case TB_KEY_CTRL_A:
      move_start (cmdWgt.inputBuf);
      break;

    case TB_KEY_CTRL_E:
      move_end (cmdWgt.inputBuf);
      break;

    case TB_KEY_ARROW_DOWN:
      if (ev.mod & TB_MOD_SHIFT) {
        scrollRow++;
      }
      else {
        history_next (cmdWgt.history, cmdWgt.inputBuf);
      }
      break;

    case TB_KEY_ARROW_UP:
      if (ev.mod & TB_MOD_SHIFT) {
        scrollRow = std::max (scrollRow - 1, 0);
      }
      else {
        history_prev (cmdWgt.history, cmdWgt.inputBuf);
      }
      break;

    default:
      return false;
  }

  return true;
};

inline void handle_key_event (
    AppState& state, const tb_event& ev
)
{
  if (ev.key == 0 && ev.ch) {
    // annoyingly, outside of handle_nav
    if ((ev.mod & TB_MOD_ALT) && ev.ch == 'b') {
      PLOGD << "Recieved alt-b (word-left) event";
      move_word_left (state.ui.cmd.inputBuf);
    }
    else if ((ev.mod & TB_MOD_ALT) && ev.ch == 'f') {
      PLOGD << "Recieved alt-f (word-right) event";
      move_word_right (state.ui.cmd.inputBuf);
    }
    else {
      PLOGD << std::format (
          "Recieved character input event: {}", ev.ch
      );
      handle_character_entry (state, ev);
    }
  }
  else {
    PLOGD << std::format (
        "Recieved navigation event: {}", ev.key
    );
    handle_nav (state, ev);
  }
}

// Does this need to access the
// whole appstate struct?
// (the only reason to care is sprawl
// and maintainability)
inline VoidOrErr handle_event (
    AppState& state, const tb_event& ev
)
{
  PLOGD << "Recieved event";
  if (ev.type == TB_EVENT_KEY) {
    handle_key_event (state, ev);
  }
  else if (ev.type == TB_EVENT_RESIZE) {
    handle_resize (state);
  }

  return {};
}
