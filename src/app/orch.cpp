#include "app/orch.hpp"

#include <fmt/format.h>
#include <plog/Log.h>

#include <expected>
#include <optional>
#include <string>

#include "app/event.hpp"
#include "app/state.hpp"
#include "app/widgets.hpp"
#include "backend/hts_sql.hpp"
#include "frontend/extb/extb.hpp"
#include "shared/cleanup.hpp"
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
  auto dwRet = draw_main_ui (state.ui, state.db, state.conf);
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

std::expected<AppState, int> init_tui_state (
    PileupDB& db_sink, std::optional<std::string_view> startupMsg
)
{
  PLOGD << "Initialising TUI state";

  auto locusResult = query::get_locus_data (db_sink);
  if (!locusResult) {
    return std::unexpected (locusResult.error());
  }

  auto prepResult = query::prepare_select_reads (db_sink, {});
  if (!prepResult) {
    return std::unexpected (prepResult.error());
  }
  auto startupStmt = std::move (*prepResult);
  uint32_t nRow = 0;
  // check no error on iteration,
  // get nrows (nreads).
  for (;; ++nRow) {
    const auto iterStatus = next_read (startupStmt);
    if (!iterStatus) {
      return std::unexpected (iterStatus.error());
    }
    switch (*iterStatus) {
      case query::RowIterStatus::rowAvail:
        continue;
      case query::RowIterStatus::exhausted:
        break;
    }
  }

  AppState state{
      .db = {
          .db = std::move (db_sink),
          .stmt = std::move (startupStmt),
          .nStmtRows = nRow,
          .userClause = {},
          .locusInfo = *locusResult
      }
  };
  if (startupMsg) {
    state.ui.cmd.msgBuf = *startupMsg;
  }

  return state;
}

VoidOrErr run_tui_loop (AppState& state)
{
  init_tb2();
  Cleanup shutdown ([]() { tb_shutdown(); });

  {
    // render first frame
    auto calcRet = size_widgets (state.ui);
    if (!calcRet) {
      return std::unexpected{calcRet.error()};
    }
    auto drawRet = draw_screen (state);
    if (!drawRet) {
      return std::unexpected{drawRet.error()};
    }
  }

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

    PLOGD << "Processed frame";
  }

  return {};
}
