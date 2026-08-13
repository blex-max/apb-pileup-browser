#include "app/tui.hpp"

#include <fmt/format.h>

#include <expected>
#include <optional>
#include <string>

#include "app/event.hpp"
#include "app/state.hpp"
#include "app/widgets.hpp"
#include "backend/PileupDB.hpp"
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
      TB_INPUT_ALT
  );  // | TB_INPUT_MOUSE for mouse ev
  tb_clear();
};


static VoidOrErr draw_screen (AppState& state)
{
  PLOGD << "Drawing screen";

  // TODO:
  // consider not redrawing whole
  // screen each frame. Instead,
  // hold a struct of `dirty`/`needsRedraw`
  // flags in state, and only redraw those items.
  tb_clear();

  /* draw frame */
  auto dwRet = draw_main_ui (state);
  if (!dwRet) {
    return std::unexpected{dwRet.error()};
  }

  if (state.conf.showOverlay) {
    // For help overlay,
    // and query columns overlay
    draw_overlay (state.ui.help);
  }

  tb_present();

  return {};
}

AppStateOrErr init (
    PileupDB& db, std::optional<std::string_view> startupMsg
)
{
  PLOGD << "Initialising TUI";

  AppState state{.db = std::move (db)};

  if (startupMsg) {
    state.ui.cmd.msgBuf = *startupMsg;
  }

  auto locusRet = get_locus_data (state.db);
  if (!locusRet) {
    return std::unexpected{locusRet.error()};
  }
  state.locus = std::move (*locusRet);

  auto prepRet =
      prepare_select_reads (state.db, state.query.userClause);
  if (!prepRet) {
    return std::unexpected{prepRet.error()};
  }
  state.query.stmt = std::move (*prepRet);

  init_tb2();
  auto calcRet = size_widgets (state.ui, state.conf);
  if (!calcRet) {
    return std::unexpected{calcRet.error()};
  }
  auto drawRet = draw_screen (state);
  if (!drawRet) {
    return std::unexpected{drawRet.error()};
  }
  return state;
}

// TODO: err strat?
VoidOrErr loop (AppState& state)
{
  tb_event ev{};

  while (state.conf.run) {
    tb_poll_event (&ev);
    // these two should return an error
    // here ONLY if something occurs
    // which means we should crash.
    // Otherwise should be handled
    // by telling the user.

    auto evRet = handle_event (state, ev);
    if (!evRet) {
      return std::unexpected{evRet.error()};
    }
    auto drawRet = draw_screen (state);
    if (!drawRet) {
      return std::unexpected{drawRet.error()};
    }

    PLOGD << fmt::format (
        "Processed frame {}", state.mData.c_frame
    );
    ++state.mData.c_frame;
  }

  return {};
}

void shutdown() { tb_shutdown(); }
