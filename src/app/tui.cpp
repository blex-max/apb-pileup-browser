#include "app/tui.hpp"

#include <fmt/format.h>

#include <expected>
#include <list>
#include <optional>
#include <string>

#include "app/event.hpp"
#include "app/fields.hpp"
#include "app/screen_projection.hpp"
#include "app/state.hpp"
#include "app/stringify_alignment.hpp"
#include "app/widgets.hpp"
#include "backend/PileupDB.hpp"
#include "frontend/drawing_chars.hpp"
#include "frontend/extb/extb.hpp"
#include "plog/Log.h"
#include "shared/err.hpp"

static void draw_sequence (
    const e2::Box& queryBox, size_t boxRow, sqlite3_stmt* dbRow,
    const LocusData& locus
)
{
  auto cig = get_cigar_blob (dbRow);
  auto nCig = get_ncig (dbRow);
  auto readStart = get_rstart (dbRow);
  std::optional<ExpandSequenceRefArgs> refArgs;
  if (locus.refSlice) {
    refArgs.emplace (readStart - locus.start, *(locus.refSlice));
  }
  auto alignmentSeq =
      expand_sequence (get_seq (dbRow), cig, nCig, refArgs);
  auto softClips = get_soft_clips (cig, nCig);

  // If query begins before displayed region, we need to subset the
  // string to keep it aligned with the displayed reference. If it
  // overruns to the right, write_string will just discard those chars.
  auto proj = project_onto_box (
      locus.pos, width (queryBox), get_rstart (dbRow)
  );
  auto seqStart = translate (
      nw_vertex (queryBox),
      {static_cast<int> (boxRow), proj.jOffset}
  );
  auto jBound = last (queryBox.jspan);

  auto visible = alignmentSeq.substr (proj.skipChars);
  auto written =
      e2::write_ascii_string (seqStart, jBound, visible);

  // Dim reference-matching bases ('=') so mismatches/indels stand out.
  for (size_t k = 0; k < written; ++k) {
    if (visible[k] == '=') {
      e2::add_attr (
          translate (seqStart, e2::J (static_cast<int> (k))),
          TB_DIM
      );
    }
  }

  // Soft-clip indicators go in any blank space left over on either
  // side of the drawn read, anchored against the read edge (so a
  // truncated label loses its outer end, not the end nearest the read).
  if (proj.skipChars == 0 && proj.jOffset > 0 &&
      !softClips.first.empty()) {
    auto avail = static_cast<size_t> (proj.jOffset);
    const std::string_view label = softClips.first;
    auto shown = label.size() > avail
                     ? label.substr (label.size() - avail)
                     : label;
    e2::write_ascii_string (
        translate (
            seqStart, e2::J (-static_cast<int> (shown.size()))
        ),
        seqStart.j, shown, TB_DIM
    );
  }

  auto rightEdge = seqStart.j + static_cast<int> (written);
  if (rightEdge < jBound && !softClips.second.empty()) {
    e2::write_ascii_string (
        {seqStart.i, rightEdge}, jBound, softClips.second, TB_DIM
    );
  }
}

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
    const size_t pad = width - cell.size();
    const size_t padLeft = pad / 2;
    cell = std::string (padLeft, ' ') + cell +
           std::string (pad - padLeft, ' ');
  }
  else {
    cell.resize (width, ' ');
  }
  e2::write_ascii_string ({i, j}, jAvail, cell);
  j += static_cast<int> (width);
  if (j > jAvail) {
    return j;
  }
  set (e2::GlobalCell{i, j}, ch::vertLine);
  return j + 1;
}

static void draw_data_table_header (
    const e2::JLine& headerLine,
    const std::list<const TableField*>& displayFields
)
{
  PLOGD << "Drawing table header";
  const int jAvail = last (headerLine.jspan);
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

static void draw_data_table_row (
    const e2::Box& dataBox, size_t boxRow, sqlite3_stmt* dbRow,
    const std::list<const TableField*>& displayFields
)
{
  const int jAvail = last (dataBox.jspan);
  const int i =
      first (dataBox.ispan) + static_cast<int> (boxRow);
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

static VoidOrErr run_query (AppState& state)
{
  // build statement from fragments (or use cached statement)
  // execute statement on db
  // draw reads
  // draw data table
  PLOGD << "Querying database";
  auto& stmt = state.query.stmt;
  auto& db = state.db;

  sqlite3_reset (stmt);

  auto& qBox = state.ui.main.queryBox;
  auto& dBox = state.ui.main.dataBox;
  auto& hLine = state.ui.main.headerLine;
  auto& displayFields = state.conf.dataFieldsRequested;

  draw_data_table_header (hLine, displayFields);

  auto nRow = height (qBox);

  size_t iRead = 0;
  size_t iRow = 0;
  for (; iRow < nRow; iRead++) {
    // NOTE: crash?
    auto nrRet = next_read (stmt, db);
    if (!nrRet) {
      return std::unexpected{nrRet.error()};
    }
    if (!(*nrRet)) {
      break;  // reads exhausted
    }
    if (iRead < state.ui.main.rowStart) {
      continue;  // scrolling
    }
    draw_sequence (qBox, iRow, stmt, state.locus);
    draw_data_table_row (dBox, iRow, stmt, displayFields);
    ++iRow;
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
      TB_INPUT_ALT
  );  // | TB_INPUT_MOUSE for mouse ev
  tb_clear();
};

static void draw_crosshair (const PileupWgt& pWgt)
{
  auto& queryBox = pWgt.queryBox;
  // ILine.j is a global column (see extb/box/box.hpp), unlike JLine's
  // jspan -- so the box-local center column must be offset by the box's
  // own global start to land on the same column draw_sequence treats as
  // pileupPos (first(queryBox.jspan) + boxWidth/2).
  auto pileupJ = first (queryBox.jspan) +
                 static_cast<int> (width (queryBox) / 2);
  add_attr (e2::ILine{queryBox.ispan, pileupJ}, TB_REVERSE);
  set (
      e2::GlobalCell{first (queryBox.ispan) - 1, pileupJ}, '|',
      TB_DIM
  );
}

static void draw_overlay (const OverlayWgt& oWgt)
{
  const auto& box = oWgt.contentBox;
  const auto& frame = oWgt.frame;
  const auto& content = oWgt.content;

  // NOTE: a nice property of the global only/
  // single surface drawing approach. Clearing
  // this layer clears everything "below".
  clear (box);
  clear (frame);

  set (north_edge (frame), ch::horzLine);
  set (east_edge (frame), ch::vertLine);
  set (south_edge (frame), ch::horzLine);
  set (west_edge (frame), ch::vertLine);

  set (nw_vertex (frame), ch::topLeftRoundCorner);
  set (ne_vertex (frame), ch::topRightRoundCorner);
  set (sw_vertex (frame), ch::bottomLeftRoundCorner);
  set (se_vertex (frame), ch::bottomRightRoundCorner);

  e2::write_ascii_string (
      translate (nw_vertex (frame), e2::J (3)), last (box.jspan),
      " q: close overlay ", TB_DIM
  );

  e2::write_ascii_string (
      nw_vertex (box), last (box.jspan), content
  );
}

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
  draw_widgets (state);
  auto dsRet = run_query (state);
  if (!dsRet) {
    return std::unexpected{dsRet.error()};
  }
  draw_crosshair (state.ui.main);

  if (state.conf.showOverlay) {
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
  auto calcRet = calc_all_widgets (state.ui, state.conf);
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
