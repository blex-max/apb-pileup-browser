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

// TODO: maybe this should move into widgets idk
static TuiStatus draw_screen (AppState& state)
{
  PLOGD << "Drawing screen";

  // TODO:
  // consider not redrawing whole
  // screen each frame. Instead,
  // hold a struct of `dirty`/`needsRedraw`
  // flags in state, and only redraw those items.
  tb_clear();

  /* draw frame */
  const auto dmuStatus =
      draw_main_ui (state.ui, state.db, state.conf);
  switch (dmuStatus.code) {
    case TuiStatus::success:
      break;
    case TuiStatus::insufficientSz:
    case TuiStatus::sqlFail:
      return dmuStatus;
  }

  if (state.conf.showOverlay) {
    // For help overlays
    draw_overlay (state.ui.overlay);
  }

  tb_present();

  return {.code = TuiStatus::success, .sqlRc = std::nullopt};
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
  // count, and as a consequence verify.
  auto rowCountResult = query::count_rows (startupStmt);
  if (!rowCountResult) {
    return std::unexpected (rowCountResult.error());
  }
  const uint32_t nRow = *rowCountResult;

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

TuiStatus run_tui_loop (AppState& state)
{
  init_tb2();
  Cleanup shutdown ([]() { tb_shutdown(); });

  {
    // render first frame
    if (!size_widgets (state.ui)) {
      return {TuiStatus::insufficientSz, std::nullopt};
    };
    switch (const auto drawStatus = draw_screen (state);
            drawStatus.code) {
      case TuiStatus::success:
        break;
      case TuiStatus::insufficientSz:
      case TuiStatus::sqlFail:
        return drawStatus;
    }
    e2::write_string (
        first (state.ui.cmd.inputLine),
        last (state.ui.cmd.inputLine.xspan), "command [args...]",
        TB_DIM
    );
    // show command line startup message
    tb_present();
  }

  tb_event ev{};
  while (state.conf.run) {
    tb_poll_event (&ev);
    switch (const auto evStatus = handle_event (state, ev);
            evStatus.code) {
      case TuiStatus::success:
      case TuiStatus::insufficientSz:
        // do nothing - allow user to resize terminal
        // rather than crashing.
        break;
      case TuiStatus::sqlFail:
        return evStatus;
    }
    switch (const auto drawStatus = draw_screen (state);
            drawStatus.code) {
      case TuiStatus::success:
      case TuiStatus::insufficientSz:
        break;
      case TuiStatus::sqlFail:
        return drawStatus;
    }

    PLOGD << "Processed frame";
  }

  return {};
}
