#include "widgets.hpp"

#include <fmt/format.h>
#include <plog/Log.h>

#include <cmath>
#include <cstdint>

#include "frontend/drawing_chars.hpp"
#include "frontend/extb/box/box.hpp"
#include "frontend/extb/extb.hpp"
#include "shared/err.hpp"

// --- helpers --- //

// Project genomic coordinate onto Box J axis where box is centered
// centered on `boxCenterGPos` in context of drawing sequence string.
// XOR fields; only one of two is ever nonzero
struct ScreenProjection {
  size_t skipChars = 0;
  int jOffset = 0;
};

static ScreenProjection align_seq_to_box (
    int64_t boxCenterGPos, size_t boxWidth, int64_t seqGStart
)
{
  const int64_t leftmostVisibleGPos =
      boxCenterGPos - (static_cast<int64_t> (boxWidth) / 2);
  const int64_t distBoxEdgeToContentStart =
      seqGStart - leftmostVisibleGPos;
  if (distBoxEdgeToContentStart < 0) {
    return {
        .skipChars =
            static_cast<size_t> (-distBoxEdgeToContentStart),
        .jOffset = 0
    };
  }
  return {
      .skipChars = 0,
      .jOffset = static_cast<int> (distBoxEdgeToContentStart)
  };
}

// --- end helpers --- //

// --- size calculation --- //

static void set_screen_size (UIBundle& ui)
{
  ui.screenH = tb_height();
  ui.screenW = tb_width();
}
static std::pair<int, int> get_screen_size (UIBundle& ui)
{
  return {ui.screenH, ui.screenW};
}

void set_overlay_widget (UIBundle& ui, TextBlockRef content)
{
  if (content.empty()) {
    return;
  }
  auto& oWgt = ui.help;
  const auto [screenH, screenW] = get_screen_size (ui);

  // dynamically sized to content
  const auto framedContentH =
      static_cast<int16_t> (content.size() + 2);
  const auto helpH = std::min (
      framedContentH, static_cast<int16_t> (std::ceil (
                          static_cast<double> (screenH) * 0.6
                      ))
  );

  const auto framedContentW =
      static_cast<int16_t> (content.front().size() + 2);
  const auto helpW = std::min (
      framedContentW, static_cast<int16_t> (std::ceil (
                          static_cast<double> (screenW) * 0.6
                      ))
  );

  const auto iOff =
      static_cast<int> (std::floor ((screenH - helpH) / 2));
  const auto jOff =
      static_cast<int> (std::floor ((screenW - helpW) / 2));

  e2::Span hISpan{iOff, iOff + helpH};
  e2::Span hJSpan{jOff, jOff + helpW};

  oWgt.frame = e2::Box{hISpan, hJSpan};
  oWgt.contentBox = e2::Box{body (hISpan), body (hJSpan)};
  oWgt.content = content;
}

void draw_overlay (const OverlayWgt& oWgt)
{
  const auto& box = oWgt.contentBox;
  const auto& frame = oWgt.frame;
  const auto& content = oWgt.content;

  // NOTE: a nice property of the global only/
  // single surface drawing approach. Clearing
  // this layer clears everything "below".
  clear (box);
  clear (frame);

  set (north_edge (frame), boxch::horzLine);
  set (east_edge (frame), boxch::vertLine);
  set (south_edge (frame), boxch::horzLine);
  set (west_edge (frame), boxch::vertLine);

  set (nw_vertex (frame), boxch::topLeftRoundCorner);
  set (ne_vertex (frame), boxch::topRightRoundCorner);
  set (sw_vertex (frame), boxch::bottomLeftRoundCorner);
  set (se_vertex (frame), boxch::bottomRightRoundCorner);

  auto jEnd = last (box.jspan);

  auto headCurs = nw_vertex (frame);
  headCurs.j += 1;
  headCurs.j += e2::write_ascii_string (
      headCurs, jEnd, " q: close overlay ", TB_DIM
  );
  headCurs.j += 3;
  const auto lnN = height (box);
  if (lnN < content.size()) {
    e2::write_ascii_string (
        headCurs, jEnd, "Up / Down: scroll", TB_DIM
    );
  }

  const auto lnOff = static_cast<size_t> (oWgt.contentLnOffset);
  auto lnI = extb::nw_vertex (box);
  for (size_t i = 0; i < lnN && i < content.size(); ++i) {
    e2::write_ascii_string (lnI, jEnd, content[i + lnOff]);
    ++lnI.i;
  }
}


// precondition: ui.main.frame is set
void size_browser_panes (BrowserWgt& pWgt, double seqPaneFrac)
{
  PLOGD << "Sizing browser child panes";

  const auto& pWgtISpan = pWgt.frame.ispan;
  const auto& pWgtJSpan = pWgt.frame.jspan;

  auto vSplitJ = static_cast<int> (ceil (
      static_cast<double> (size (pWgtJSpan) - 2) * seqPaneFrac
  ));
  pWgt.vSep = {
      section (pWgtISpan, 0, size (pWgtISpan) - 1), vSplitJ
  };
  pWgt.refLine = {
      first (pWgtISpan) + 1, section (pWgtJSpan, 1, vSplitJ)
  };
  pWgt.headerLine = {
      first (pWgtISpan) + 1, {vSplitJ + 1, last (pWgtJSpan) - 1}
  };
  pWgt.headerSep = {
      first (pWgtISpan) + 2, pWgtJSpan
  };  // overlapping, to set connectors
  // stop one row short of querySep's row (mainI.last - 2), so content
  // rows and the separator below them don't share a row -- mirrors
  // headerLine/headerSep/content-start-at-+3 above.
  pWgt.queryBox = {
      section (pWgtISpan, 3, size (pWgtISpan) - 2),
      section (pWgtJSpan, 1, vSplitJ)
  };
  pWgt.dataBox = {
      section (pWgtISpan, 3, size (pWgtISpan) - 2),
      {vSplitJ + 1, last (pWgtJSpan) - 1}
  };
  pWgt.querySep = {pWgtISpan.last - 2, pWgtJSpan};
  pWgt.infoLine = {pWgtISpan.last - 1, body (pWgtJSpan)};
}

// precondition: widget frame is set
static void size_cmd_widget (CmdWgt& cWgt)
{
  const auto& [cmdI, cmdJ] = spans (cWgt.frame);

  auto i = cmdI.first + 1;
  cWgt.queryStatusLine = e2::JLine{i++, body (cmdJ)};
  cWgt.statusSep = e2::JLine{
      i++,
      cmdJ  // include frame, to draw pipe connectors at line ends
  };

  // cmd input
  cWgt.inputCaret = e2::GlobalCell{i, cmdJ.first + 1};
  cWgt.inputLine = e2::JLine{
      i++,
      {cmdJ.first + 2,  // skip border, leave space for caret :
       cmdJ.last - 1}
  };

  cWgt.sepLine = e2::JLine{
      i++,
      cmdJ  // include frame, to draw pipe connectors at line ends
  };

  cWgt.msgLine = e2::JLine{i, body (cmdJ)};
}

VoidOrErr size_widgets (UIBundle& ui, double seqPaneFrac)
{
  PLOGD << "Calculating widget size";

  set_screen_size (ui);
  const auto [screenH, screenW] = get_screen_size (ui);

  const e2::Span screenI{0, screenH};
  e2::Span screenJ{0, screenW};

  // vertical sectioning of terminal
  const e2::Span mainI{screenI.first, screenI.last - CMD_H};
  const e2::Span cmdI{mainI.last, mainI.last + CMD_H};

  PLOGD << "screen i last: " << screenI.last;
  PLOGD << "cmd i first: " << cmdI.first;
  PLOGD << "cmd i last: " << cmdI.last;

  if (!e2::valid (screenI) || !e2::valid (screenJ) ||
      !e2::valid (mainI) || !e2::valid (cmdI)) {
    return std::unexpected (make_internal_err (
        "Could not calculate widgets. Terminal likley too small!"
    ));
  }

  // widgets to calc
  auto& pWgt = ui.main;
  pWgt.frame = e2::Box{mainI, screenJ};

  size_browser_panes (pWgt, seqPaneFrac);

  auto& cWgt = ui.cmd;
  cWgt.frame = e2::Box{cmdI, screenJ};

  size_cmd_widget (cWgt);

  // for resize
  set_overlay_widget (ui, ui.help.content);

  return {};
}

// --- end sizing --- //

// --- draw shared layout --- //

static void draw_shared_layout (BrowserWgt& pWgt, CmdWgt& cWgt)
{
  PLOGD << "Drawing layout";

  auto& pFrame = pWgt.frame;
  set (nw_vertex (pFrame), boxch::topLeftRoundCorner, TB_DIM);
  set (ne_vertex (pFrame), boxch::topRightRoundCorner, TB_DIM);

  set (body (north_edge (pFrame)), boxch::horzLine, TB_DIM);
  // main frame is open at the bottom (the cmd frame closes it), so the
  // side edges skip only the top corner and run to the last row.
  set (
      section (west_edge (pFrame), 1, height (pFrame)),
      boxch::vertLine, TB_DIM
  );
  set (
      section (east_edge (pFrame), 1, height (pFrame)),
      boxch::vertLine, TB_DIM
  );

  set (body (pWgt.headerSep), boxch::horzLine, TB_DIM);
  set (first (pWgt.headerSep), boxch::rightTConnect, TB_DIM);

  set (body (pWgt.querySep), boxch::horzLine, TB_DIM);
  set (first (pWgt.querySep), boxch::rightTConnect, TB_DIM);
  set (last (pWgt.querySep), boxch::leftTConnect, TB_DIM);

  set (body (pWgt.vSep), boxch::vertLine, TB_DIM);
  set (first (pWgt.vSep), boxch::downTConnect, TB_DIM);
  set (last (pWgt.vSep), boxch::upTConnect, TB_DIM);

  set (last (pWgt.headerSep), boxch::leftTConnect, TB_DIM);


  auto& cFrame = cWgt.frame;
  set (nw_vertex (cFrame), boxch::topLeftRoundCorner, TB_DIM);
  set (ne_vertex (cFrame), boxch::topRightRoundCorner, TB_DIM);
  set (sw_vertex (cFrame), boxch::bottomLeftRoundCorner, TB_DIM);
  set (
      se_vertex (cFrame), boxch::bottomRightRoundCorner, TB_DIM
  );

  set (body (north_edge (cFrame)), boxch::horzHeavy, TB_DIM);
  set (body (west_edge (cFrame)), boxch::vertLine, TB_DIM);
  set (body (east_edge (cFrame)), boxch::vertLine, TB_DIM);
  set (body (cWgt.statusSep), boxch::horzLine, TB_DIM);

  set (cWgt.inputCaret, ':');
  set (body (cWgt.sepLine), boxch::horzLine, TB_DIM);
}

// --- end draw shared layout --- //

// --- draw browser pane --- //

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
  set (e2::GlobalCell{i, j}, boxch::vertLine);
  return j + 1;
}

static void draw_data_table_header (
    const e2::JLine& headerLine,
    const std::list<const DataTableCol*>& displayFields
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
    const std::list<const DataTableCol*>& displayFields
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

// draw sequence to seq pane
static void draw_sequence (
    const e2::Box& queryBox, size_t boxRow, sqlite3_stmt* dbRow,
    const LocusData& locus
)
{
  // NOTE: inlining to a single function
  // makes it easier to extend and maintain
  // drawing logic. Resist urge to modularise.

  const auto& ref = locus.refSlice;
  const auto readStart = get_rstart (dbRow);

  // NOTE: since view is centered on pileup,
  // all reads should always be at least partially in view
  // (unless pane is folded)
  const int64_t boxLeftEdgeGPos =
      locus.pos - (static_cast<int64_t> (width (queryBox)) / 2);
  const int64_t startToLEdge = readStart - boxLeftEdgeGPos;

  const auto* cig = get_cigar_blob (dbRow);
  const auto nCig = get_ncig (dbRow);
  const auto rawSeq = get_seq (dbRow);

  auto writeHead =
      e2::GlobalCell{nw_vertex (queryBox) + e2::dI (boxRow)};
  if (startToLEdge > 0) {
    writeHead.j += startToLEdge;
  }
  auto iGc = readStart;  // current genomic coordinate
  size_t iQuery = 0;
  // locus start always <= readStart
  int64_t iRef = ref ? readStart - locus.start : 0;
  for (size_t iOp = 0; iOp < nCig; iOp++) {
    const auto op = cig[iOp];
    const auto opSz = bam_cigar_oplen (op);
    const auto opType = bam_cigar_op (op);
    const auto opConsumeType = bam_cigar_type (op);

    if (opConsumeType == 0b11) {
      // consumes query and ref

      if ((iGc + opSz) >= boxLeftEdgeGPos) {
        // op at least partially on screen
        int64_t skipOpBases = 0;
        auto opLenRemain = opSz;
        if (iGc < boxLeftEdgeGPos) {
          // op partially on screen only
          skipOpBases = boxLeftEdgeGPos - iGc;  // +ve
          iQuery += skipOpBases;
          iRef += skipOpBases;
          opLenRemain -= skipOpBases;
        }

        // set bases
        // masking bases that match the reference as '='.
        for (size_t i = 0; i < opLenRemain &&
                           writeHead.j < last (queryBox.jspan);
             ++i) {
          uintattr_t dispAttr = 0;
          auto dispChar = rawSeq[iQuery + i];
          if (ref && (dispChar ==
                      (*ref)[static_cast<size_t> (iRef) + i])) {
            dispChar = '=';
            dispAttr = TB_DIM;
          }
          set (
              writeHead, static_cast<uint32_t> (dispChar),
              dispAttr
          );
          ++writeHead.j;
        }

        iQuery += opLenRemain;
        iRef += opLenRemain;
        iGc += opSz;
      }
      else {
        // op entirely offscreen
        iQuery += opSz;
        iRef += opSz;
        iGc += opSz;
      }
    }
    else if (opConsumeType == 0b10) {
      // consumes ref only
      // Deletions, or "skipped region"
      //     - the latter only relevant to RNA (TODO: disambiguate?)
      // don't advance query tracker

      if ((iGc + opSz) >= boxLeftEdgeGPos) {
        // op at least partially on screen
        int64_t skipOpBases = (iGc < boxLeftEdgeGPos)
                                  ? (boxLeftEdgeGPos - iGc)
                                  : 0;
        for (size_t i = skipOpBases;
             i < opSz && writeHead.j < last (queryBox.jspan);
             ++i) {
          set (writeHead, '-');
          ++writeHead.j;
        }
      }
      iRef += opSz;
      iGc += opSz;
    }
    else if (opConsumeType == 0b01) {
      // consumes query only

      if (opType == BAM_CINS && iGc > boxLeftEdgeGPos) {
        // insertion
        // where at least one base PRIOR
        // to the insertion is visible

        // modify anchor base
        const auto anchorCell = writeHead - e2::dJ (1);
        // removes other styling
        e2::set_attr (anchorCell, TB_UNDERLINE);
        e2::extend (anchorCell, markch::ringAbove);
      }

      if (opType == BAM_CSOFT_CLIP && iOp == 0 &&
          startToLEdge > 0) {
        // soft clipping at start of read
        // where cells available for writing
        // clipping label
        std::string clipLabel =
            "s(" + std::to_string (opSz) + ")";
        if (startToLEdge < clipLabel.size()) {
          clipLabel =
              clipLabel.substr (clipLabel.size() - startToLEdge);
        }
        e2::write_ascii_string (
            writeHead -
                e2::dJ (static_cast<int> (clipLabel.size())),
            last (queryBox.jspan), clipLabel, TB_DIM
        );
      }
      if (opType == BAM_CSOFT_CLIP && iOp == (nCig - 1)) {
        // soft clipping at end of read
        std::string clipLabel =
            "s(" + std::to_string (opSz) + ")";
        // if no space left, no-op
        e2::write_ascii_string (
            writeHead, last (queryBox.jspan), clipLabel, TB_DIM
        );
      }

      iQuery += opSz;
    }
    else {
      // hard clip/padding not handled
      continue;
    }

    if (writeHead.j >= last (queryBox.jspan)) {
      // early exit if row exhausted
      break;
    }
  }
}

static VoidOrErr draw_query_data (
    BrowserWgt& pWgt, DynamicSelectReadsStmt& stmt,
    const PileupDB& db, const LocusData& locus,
    const DataRequestList& displayCols
)
{
  // draw reads and data table

  sqlite3_reset (stmt);

  auto& qBox = pWgt.queryBox;
  auto& dBox = pWgt.dataBox;
  auto& hLine = pWgt.headerLine;

  draw_data_table_header (hLine, displayCols);

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
    if (iRead < pWgt.rowStart) {
      continue;  // scrolling
    }
    draw_sequence (qBox, iRow, stmt, locus);
    draw_data_table_row (dBox, iRow, stmt, displayCols);
    ++iRow;
  }

  auto pileupJ =
      first (qBox.jspan) + static_cast<int> (width (qBox) / 2);
  add_attr (e2::ILine{qBox.ispan, pileupJ}, TB_REVERSE);
  set (
      e2::GlobalCell{first (qBox.ispan) - 1, pileupJ}, '|',
      TB_DIM
  );

  return {};
}

static void draw_pileup_ambient (
    BrowserWgt& pWgt, const LocusData& locusData
)
{
  // TODO: get rid of projection function (?)
  if (locusData.refSlice) {
    auto proj = align_seq_to_box (
        locusData.pos, size (pWgt.refLine), locusData.start
    );

    e2::write_ascii_string (
        {pWgt.refLine.i,
         first (pWgt.refLine.jspan) + proj.jOffset},
        last (pWgt.refLine.jspan),
        locusData.refSlice->substr (proj.skipChars)
    );
  }

  // locus info
  {
    auto cursor = first (pWgt.infoLine);
    const auto lineEnd = last (pWgt.infoLine.jspan);
    cursor.j++;  // initial space
    cursor.j += e2::write_ascii_string (
        cursor, lineEnd, "LOCUS:", TB_DIM
    );
    cursor.j++;  // space
    cursor.j += e2::write_ascii_string (
        cursor, lineEnd,
        fmt::format ("{}:{}", locusData.contig, locusData.pos)
    );
    cursor.j++;  // space
    set (cursor, boxch::vertLine, TB_DIM);
    cursor.j += 2;  // past bar, then space
    cursor.j += e2::write_ascii_string (
        cursor, lineEnd, "SPAN:", TB_DIM
    );
    cursor.j++;  // space
    cursor.j += e2::write_ascii_string (
        cursor, lineEnd,
        fmt::format ("{}-{}", locusData.start, locusData.end)
    );
    cursor.j++;  // space
    set (cursor, boxch::vertLine, TB_DIM);
  }
}

static VoidOrErr draw_piluep (
    BrowserWgt& pWgt, DynamicSelectReadsStmt& stmt,
    const PileupDB& db, const LocusData& locus,
    const DataRequestList& displayCols
)
{
  auto dqRet =
      draw_query_data (pWgt, stmt, db, locus, displayCols);
  if (!dqRet) {
    return std::unexpected (dqRet.error());
  }

  draw_pileup_ambient (pWgt, locus);

  return {};
}

// --- end draw browser pane --- //


static void draw_cmd_ambient (
    CmdWgt& cWgt, const DynamicFragments& userQuery
)
{
  e2::write_ascii_string (
      first (cWgt.inputLine), last (cWgt.inputLine).j,
      cWgt.inputBuf.text
  );
  auto cursorCell =
      first (cWgt.inputLine) +
      e2::dJ (static_cast<int> (cWgt.inputBuf.curs));
  if (cursorCell.j < last (cWgt.inputLine).j) {
    e2::add_attr (cursorCell, TB_REVERSE);
  }
  e2::write_ascii_string (
      first (cWgt.msgLine), last (cWgt.msgLine).j, cWgt.msgBuf,
      TB_DIM
  );

  // stringify query
  std::string userClauseString;
  if (!userQuery.where.empty()) {
    userClauseString.append ("WHERE ");
    for (size_t i = 0; i < userQuery.where.size(); ++i) {
      userClauseString.append (userQuery.where[i]);
      if (i != (userQuery.where.size() - 1)) {
        userClauseString.append (" ");
      }
    }
  }

  if (!userQuery.orderBy.empty()) {
    userClauseString.append (" ORDER BY ");
    userClauseString.append (userQuery.orderBy);
  }

  e2::write_ascii_string (
      first (cWgt.queryStatusLine),
      last (cWgt.queryStatusLine).j, userClauseString, TB_DIM
  );
}

VoidOrErr draw_main_ui (
    UIBundle& ui, DBBundle& db,
    const DataRequestList& colsRequested
)
{
  // NOTE: set order does matter,
  // since some places just overwrite
  // previous draw calls
  PLOGD << "Drawing widgets";


  // NOTE: Worry about border stylisation last!
  draw_shared_layout (ui.main, ui.cmd);

  auto dpRet = draw_piluep (
      ui.main, db.stmt, db.db, db.locus, colsRequested
  );
  if (!dpRet) {
    // TODO: not really well thought out.
    return std::unexpected (dpRet.error());
  }

  draw_cmd_ambient (ui.cmd, db.userClause);

  return {};
}
