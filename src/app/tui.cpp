#include "app/tui.hpp"

#include <expected>
#include <format>
#include <list>
#include <string>

#include "app/event.hpp"
#include "app/fields.hpp"
#include "app/widgets.hpp"
#include "backend/PileupDB.hpp"
#include "backend/PileupDB_internal.hpp"
#include "frontend/drawing_chars.hpp"
#include "frontend/extb/extb.hpp"
#include "plog/Log.h"
#include "shared/err.hpp"

void draw_sequence (
    const e2::Box& queryBox, size_t boxRow, sqlite3_stmt* dbRow
)
{
  // pull cigar blob, n cigar operations, sequence text, and quality text
  // using accessors.
  // draw (unaligned for now) read to screen
  auto rSeq = get_seq (dbRow);
  e2::write_string (
      {first (queryBox.ispan) + static_cast<int> (boxRow),
       first (queryBox.jspan)},
      last (queryBox.jspan), rSeq
  );
}

// Draws `text` padded/truncated to exactly `width` characters at {i, j},
// followed by a vertLine separator. Always advances the cursor by `width`
// (not by the resulting string's length), and returns the j just past the
// separator, so header and row drawing (which must land in the exact same
// columns) can both drive their cursor off this single function instead
// of recomputing offsets independently. `center` pads text on both sides
// (used for headers); row cells stay left-aligned.
static int draw_table_cell (
    int i, int j, int jAvail, const std::string& text,
    size_t width, bool center = false
)
{
  std::string cell = text;
  if (cell.size() > width) {
    cell.resize (width);
  }
  else if (center) {
    size_t pad = width - cell.size();
    size_t padLeft = pad / 2;
    cell = std::string (padLeft, ' ') + cell +
           std::string (pad - padLeft, ' ');
  }
  else {
    cell.resize (width, ' ');
  }
  e2::write_string ({i, j}, jAvail, cell);
  j += static_cast<int> (width);
  if (j > jAvail) {
    return j;
  }
  set (e2::GlobalCell{i, j}, ch::vertLine);
  return j + 1;
}

void draw_data_table_header (
    const e2::JLine& headerLine,
    const std::list<const TableField*>& displayFields
)
{
  PLOGD << "Drawing table header";
  int jAvail = last (headerLine.jspan);
  int j = first (headerLine.jspan);
  for (const auto* f : displayFields) {
    j = draw_table_cell (
        headerLine.i, j, jAvail, f->name, f->width, true
    );
    if (j > jAvail) {
      break;
    }
  }
}

void draw_data_table_row (
    const e2::Box& dataBox, size_t boxRow, sqlite3_stmt* dbRow,
    const std::list<const TableField*>& displayFields
)
{
  int jAvail = last (dataBox.jspan);
  int i = first (dataBox.ispan) + static_cast<int> (boxRow);
  int j = first (dataBox.jspan);
  for (const auto* f : displayFields) {
    j = draw_table_cell (
        i, j, jAvail, f->retrieve_from_db (dbRow), f->width
    );
    if (j > jAvail) {
      break;
    }
  }
}

VoidOrErr run_query (AppState& state)
{
  // build statement from fragments (or use cached statement)
  // execute statement on db
  // draw reads
  // draw data table
  PLOGD << "Querying database";
  auto& stmt = state.query.stmt;
  auto& db = state.db;

  // I think the next read function
  // just obscures what's happening here
  // and makes the reset confusing.
  sqlite3_reset (stmt);

  auto& qBox = state.ui.main.queryBox;
  auto& dBox = state.ui.main.dataBox;
  auto& hLine = state.ui.main.headerLine;
  auto& displayFields = state.conf.dataFieldsRequested;

  draw_data_table_header (hLine, displayFields);

  auto nRow = height (qBox);
  for (size_t j = 0; (j < (nRow - 1)); j++) {
    // NOTE: crash?
    auto nrRet = next_read (stmt, db);
    if (!nrRet) {
      return std::unexpected{nrRet.error()};
    }
    const auto readAvail = *nrRet;
    if (!readAvail) {
      break;
    }
    draw_sequence (qBox, j, stmt);
    draw_data_table_row (dBox, j, stmt, displayFields);
  }

  return {};
}

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

  // TODO:
  // consider not redrawing whole
  // screen each frame. Instead,
  // hold a struct of `dirty`/`needsRedraw`
  // flags in state, and only redraw those items.
  tb_clear();

  /* draw frame */
  draw_widgets (state);
  auto dsRet = run_query (state);
  if (!dsRet) {
    return std::unexpected{dsRet.error()};
  }

  tb_present();

  return {};
}

AppStateOrErr init (PileupDB& db)
{
  PLOGD << "Initialising TUI";

  AppState state{.db = std::move (db)};

  auto prepRet =
      prepare_select_reads (state.db, state.query.userClause);
  if (!prepRet) {
    return std::unexpected{prepRet.error()};
  }
  state.query.stmt = std::move (*prepRet);

  init_tb2();
  auto calcRet = calc_widgets (state.ui, state.conf);
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

    PLOGD << std::format (
        "Processed frame {}", state.mData.c_frame
    );
    ++state.mData.c_frame;
  }

  return {};
}

void shutdown() { tb_shutdown(); }
