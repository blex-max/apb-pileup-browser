#include "app/tui.hpp"

#include <format>

#include "app/event.hpp"
#include "app/widgets.hpp"
#include "frontend/extb/extb.hpp"
#include "plog/Log.h"
#include "shared/err.hpp"

static void init_tb2()
{
  PLOGD << "Initialising termbox2";
  // error possiblity?
  setlocale (LC_ALL, "");

  tb_init();
  tb_set_input_mode (
      TB_INPUT_ESC
  );  // | TB_INPUT_MOUSE for mouse ev
  tb_clear();
};

VoidOrErr draw_screen (AppState& state)
{
  PLOGD << "Drawing screen";
  // tb_clear();

  /* draw frame */
  auto dwRet = draw_widgets (state);
  if (!dwRet) {
    return std::unexpected{dwRet.error()};
  }

  tb_present();

  return {};
}

AppStateOrErr init()
{
  PLOGD << "Initialising TUI";

  AppState state{};
  init_tb2();
  calc_widgets (state.ui, state.conf);
  draw_screen (state);
  return state;
}

// TODO: err strat?
VoidOrErr loop (AppState& state)
{
  tb_event ev{};

  while (state.conf.run) {
    tb_poll_event (&ev);
    handle_event (state, ev);
    draw_screen (state);

    PLOGD << std::format (
        "Processed frame {}", state.mData.c_frame
    );
    ++state.mData.c_frame;
  }

  return {};
}

void shutdown (AppState& _) { tb_shutdown(); }
