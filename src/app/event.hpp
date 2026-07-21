#pragma once

#include "app/cmd.hpp"
#include "app/state.hpp"
#include "app/widgets.hpp"
#include "frontend/extb/extb.hpp"
#include "shared/err.hpp"

inline VoidOrErr handle_resize (AppState& state)
{
  calc_widgets (state.ui, state.conf);

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

  switch (ev.key) {
    case TB_KEY_ENTER:
      // execute user command
      if (!cmdWgt.inputBuf.text.empty()) {
        cmdWgt.msgBuf = exec_cmd (cmdWgt.inputBuf.text, state)
                            .msg;  // return msg
        clear (cmdWgt.inputBuf);
      }
      break;

    case TB_KEY_BACKSPACE:
    case TB_KEY_BACKSPACE2:
      del_back (cmdWgt.inputBuf);
      break;

    // TODO: more cases

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
    PLOGD << "Recieved character input event";
    handle_character_entry (state, ev);
  }
  else {
    PLOGD << "Recieved navigation event";
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
