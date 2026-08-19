#include "widgets.hpp"

#include <fmt/format.h>
#include <plog/Log.h>

#include <cmath>
#include <cstdint>
#include <iterator>

#include "app/data_table_cols.hpp"
#include "frontend/drawing_chars.hpp"
#include "frontend/extb/box/box.hpp"
#include "frontend/extb/extb.hpp"
#include "shared/err.hpp"

// --- helpers --- //

// Project genomic coordinate onto Box X axis where box is centered
// centered on `boxCenterGPos` in context of drawing sequence string.
// XOR fields; only one of two is ever nonzero
struct ScreenProjection {
  size_t skipChars = 0;
  int xOffset = 0;
};

static ScreenProjection align_seq_to_box (
    int64_t boxCenterGPos, int boxWidth, int64_t seqGStart
)
{
  // TODO: probably inline this and remove func
  const int64_t leftmostVisibleGPos =
      boxCenterGPos - (boxWidth / 2);
  const int64_t distBoxEdgeToContentStart =
      seqGStart - leftmostVisibleGPos;
  if (distBoxEdgeToContentStart < 0) {
    return {
        .skipChars =
            static_cast<size_t> (-distBoxEdgeToContentStart),
        .xOffset = 0
    };
  }
  return {
      .skipChars = 0,
      .xOffset = static_cast<int> (distBoxEdgeToContentStart)
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
  return {ui.screenW, ui.screenH};
}

void set_overlay_widget (UIBundle& ui, TextBlockRef content)
{
  if (content.empty()) {
    return;
  }
  auto& oWgt = ui.help;
  const auto [screenW, screenH] = get_screen_size (ui);

  // dynamically sized to content
  const auto framedContentH =
      static_cast<int> (content.size() + 2);
  const auto helpH = std::min<int> (
      framedContentH, static_cast<int> (std::ceil (
                          static_cast<double> (screenH) * 0.6
                      ))
  );

  const auto framedContentW =
      static_cast<int> (content.front().size() + 2);
  const auto helpW = std::min (
      framedContentW, static_cast<int> (std::ceil (
                          static_cast<double> (screenW) * 0.6
                      ))
  );

  const auto xOff =
      static_cast<int> (std::floor ((screenW - helpW) / 2));
  const auto yOff =
      static_cast<int> (std::floor ((screenH - helpH) / 2));

  e2::Span ySpan{yOff, yOff + helpH};
  e2::Span xSpan{xOff, xOff + helpW};

  oWgt.frame = e2::Box{xSpan, ySpan};
  oWgt.contentBox = e2::Box{body (xSpan), body (ySpan)};
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

  set (edgeAB (frame), boxch::horzLine);
  set (edgeBC (frame), boxch::vertLine);
  set (edgeCD (frame), boxch::horzLine);
  set (edgeDA (frame), boxch::vertLine);

  set (vertexA (frame), boxch::topLeftRoundCorner);
  set (vertexB (frame), boxch::topRightRoundCorner);
  set (vertexC (frame), boxch::bottomLeftRoundCorner);
  set (vertexD (frame), boxch::bottomRightRoundCorner);

  auto xEnd = last (box.xspan);

  auto writeHead = vertexA (frame);
  writeHead.x += 1;
  writeHead.x += e2::write_ascii_string (
      writeHead, xEnd, " q: close overlay ", TB_DIM
  );
  writeHead.x += 3;
  const auto lnN = height (box);
  if (lnN < std::ssize (content)) {
    e2::write_ascii_string (
        writeHead, xEnd, "Up / Down: scroll", TB_DIM
    );
  }

  const auto lnOff = static_cast<size_t> (oWgt.contentLnOffset);
  auto lnY = extb::vertexA (box);
  for (int i = 0; i < lnN && i < std::ssize (content); ++i) {
    e2::write_ascii_string (
        lnY, xEnd, content[static_cast<size_t> (i) + lnOff]
    );
    ++lnY.y;
  }
}


// precondition: ui.main.frame is set
void size_browser_panes (BrowserWgt& bWgt, double seqPaneFrac)
{
  PLOGD << "Sizing browser child panes";

  const auto& [bXSpan, bYSpan] = spans (bWgt.frame);

  const auto vSplitX = static_cast<int> (ceil (
      static_cast<double> (size (bXSpan) - 2) * seqPaneFrac
  ));

  const auto seqX = construct_relative (bXSpan, 1, vSplitX);
  const auto dataX = construct_relative (
      bXSpan, vSplitX + 1, size (bXSpan) - 1
  );
  const auto contentY =
      construct_relative (bYSpan, 3, size (bYSpan) - 2);

  bWgt.vSep = {
      first (bXSpan) + vSplitX,
      construct_relative (bYSpan, 0, size (bYSpan) - 1)
  };
  bWgt.refLine = {seqX, first (bYSpan) + 1};
  bWgt.tableHeaderLine = {dataX, first (bYSpan) + 1};
  bWgt.headerSep = {
      bXSpan, first (bYSpan) + 2
  };  // overlapping, to set connectors
  bWgt.queryBox = {seqX, contentY};
  bWgt.dataBox = {dataX, contentY};
  bWgt.querySep = {bXSpan, last (bYSpan) - 2};
  bWgt.infoLine = {body (bXSpan), last (bYSpan) - 1};
}

// precondition: widget frame is set
static void size_cmd_widget (CmdWgt& cWgt)
{
  const auto& [cmdX, cmdY] = spans (cWgt.frame);

  auto y = first (cmdY) + 1;
  cWgt.queryStatusLine = e2::HLine{body (cmdX), y++};
  cWgt.statusSep = e2::HLine{
      cmdX,  // include frame, to draw pipe connectors at line ends
      y++
  };

  // cmd input
  cWgt.inputCaret = e2::GlobalCell{first (cmdX) + 1, y};
  cWgt.inputLine = e2::HLine{
      // skip border, leave space for caret ':'
      construct_relative (cmdX, 2, size (cmdX) - 1), y++
  };

  cWgt.sepLine = e2::HLine{
      cmdX,  // include frame, to draw pipe connectors at line ends
      y++
  };

  cWgt.msgLine = e2::HLine{body (cmdX), y};
}

VoidOrErr size_widgets (UIBundle& ui, double seqPaneFrac)
{
  PLOGD << "Calculating widget size";

  set_screen_size (ui);
  const auto [screenW, screenH] = get_screen_size (ui);

  const e2::Span screenX{0, screenW};
  const e2::Span screenY{0, screenH};

  // vertical sectioning of terminal
  const e2::Span mainY{screenY.first, screenY.last - sh_cmdH};
  const e2::Span cmdY{mainY.last, mainY.last + sh_cmdH};

  PLOGD << "screen y last: " << screenY.last;
  PLOGD << "cmd y first: " << cmdY.first;
  PLOGD << "cmd y last: " << cmdY.last;

  if (!e2::valid (screenY) || !e2::valid (screenX) ||
      !e2::valid (mainY) || !e2::valid (cmdY)) {
    return std::unexpected (make_internal_err (
        "Could not calculate widgets. Terminal likley too small!"
    ));
  }

  // widgets to calc
  auto& bWgt = ui.browsr;
  bWgt.frame = e2::Box{screenX, mainY};

  size_browser_panes (bWgt, seqPaneFrac);

  auto& cWgt = ui.cmd;
  cWgt.frame = e2::Box{screenX, cmdY};

  size_cmd_widget (cWgt);

  // for resize
  set_overlay_widget (ui, ui.help.content);

  return {};
}

// --- end sizing --- //

// --- draw layout --- //

static void draw_browser_chrome (BrowserWgt& bWgt)
{
  auto& bFrame = bWgt.frame;
  set (vertexA (bFrame), boxch::topLeftRoundCorner, TB_DIM);
  set (vertexB (bFrame), boxch::topRightRoundCorner, TB_DIM);

  set (body (edgeAB (bFrame)), boxch::horzLine, TB_DIM);
  // main frame is open at the bottom (the cmd frame closes it), so the
  // side edges skip only the top corner and run to the last row.
  set (
      construct_relative (edgeDA (bFrame), 1, height (bFrame)),
      boxch::vertLine, TB_DIM
  );
  set (
      construct_relative (edgeBC (bFrame), 1, height (bFrame)),
      boxch::vertLine, TB_DIM
  );

  set (body (bWgt.headerSep), boxch::horzLine, TB_DIM);
  set (first (bWgt.headerSep), boxch::rightTConnect, TB_DIM);

  set (body (bWgt.querySep), boxch::horzLine, TB_DIM);
  set (first (bWgt.querySep), boxch::rightTConnect, TB_DIM);
  set (last (bWgt.querySep), boxch::leftTConnect, TB_DIM);

  set (body (bWgt.vSep), boxch::vertLine, TB_DIM);
  set (first (bWgt.vSep), boxch::downTConnect, TB_DIM);
  set (last (bWgt.vSep), boxch::upTConnect, TB_DIM);

  set (last (bWgt.headerSep), boxch::leftTConnect, TB_DIM);
}

static void draw_cmd_chrome (CmdWgt& cWgt)
{
  auto& cFrame = cWgt.frame;
  set (vertexA (cFrame), boxch::topLeftRoundCorner, TB_DIM);
  set (vertexB (cFrame), boxch::topRightRoundCorner, TB_DIM);
  set (vertexC (cFrame), boxch::bottomLeftRoundCorner, TB_DIM);
  set (vertexD (cFrame), boxch::bottomRightRoundCorner, TB_DIM);

  set (body (edgeAB (cFrame)), boxch::horzHeavy, TB_DIM);
  set (body (edgeDA (cFrame)), boxch::vertLine, TB_DIM);
  set (body (edgeBC (cFrame)), boxch::vertLine, TB_DIM);
  set (body (cWgt.statusSep), boxch::horzLine, TB_DIM);

  set (cWgt.inputCaret, ':');
  set (body (cWgt.sepLine), boxch::horzLine, TB_DIM);
}

static void draw_layout_chrome (BrowserWgt& bWgt, CmdWgt& cWgt)
{
  PLOGD << "Drawing layout";

  draw_browser_chrome (bWgt);
  draw_cmd_chrome (cWgt);
}

// --- end draw shared layout --- //

// --- draw browser pane --- //

static int draw_table_cell (
    int x, int y, int xAvail, const std::string_view text,
    size_t width, bool center = false
)
{
  std::string cell{text};
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
  e2::write_ascii_string ({x, y}, xAvail, cell);
  x += static_cast<int> (width);
  if (x > xAvail) {
    return x;
  }
  set (e2::GlobalCell{x, y}, boxch::vertLine);
  return x + 1;
}

static void draw_data_table_header (
    const e2::HLine& headerLine,
    const std::list<const DataTableCol*>& displayFields
)
{
  PLOGD << "Drawing table header";
  const int xAvail = last (headerLine.xspan);
  int x = first (headerLine.xspan);
  for (const auto* f : displayFields) {
    x = draw_table_cell (
        x, headerLine.y, xAvail, f->name, f->width, true
    );
    if (x > xAvail) {
      break;
    }
  }
}

static void draw_data_table_row (
    const e2::Box& dataBox, int boxRow, sqlite3_stmt* br_dbRow,
    const std::list<const DataTableCol*>& displayFields
)
{
  const int xAvail = last (dataBox.xspan);
  const int y = first (dataBox.yspan) + boxRow;
  int x = first (dataBox.xspan);
  for (const auto* f : displayFields) {
    x = draw_table_cell (
        x, y, xAvail, f->retrieve_from_db (br_dbRow), f->width
    );
    if (x > xAvail) {
      break;
    }
  }
}

// draw sequence to seq pane
static void draw_sequence (
    const e2::Box& queryBox, int boxRow, sqlite3_stmt* br_dbRow,
    const LocusData& locus
)
{
  // TODO: mode arg, for display insertions and quality
  // string in `tracks` below the read
  // NOTE: inlining to a single function
  // makes it easier to extend and maintain
  // drawing logic. Resist urge to modularise.

  const auto& ref = locus.refSlice;
  const auto readStart = get_rstart (br_dbRow);

  // NOTE: since view is centered on pileup,
  // all reads should always be at least partially in view
  // (unless pane is folded)
  const int64_t boxLeftEdgeGPos =
      locus.pos - (width (queryBox) / 2);
  const int startToLEdge =
      static_cast<int> (readStart - boxLeftEdgeGPos);

  const auto* br_cig = get_cigar_blob (br_dbRow);
  const auto nCig = get_ncig (br_dbRow);
  const auto rawSeq = get_seq (br_dbRow);

  auto writeHead =
      e2::GlobalCell{vertexA (queryBox) + e2::dY (boxRow)};
  if (startToLEdge > 0) {
    writeHead.x += startToLEdge;
  }
  auto iGc = readStart;  // current genomic coordinate
  size_t iQuery = 0;
  // locus start always <= readStart
  size_t iRef =
      ref ? static_cast<size_t> (readStart - locus.start) : 0;
  for (size_t iOp = 0; iOp < nCig; iOp++) {
    const auto op = br_cig[iOp];
    const auto opSz = bam_cigar_oplen (op);
    const auto opType = bam_cigar_op (op);
    const auto opConsumeType = bam_cigar_type (op);

    if (opConsumeType == 0b11) {
      // consumes query and ref

      if ((iGc + opSz) >= boxLeftEdgeGPos) {
        // op at least partially on screen
        size_t skipOpBases = 0;
        auto opLenRemain = opSz;
        if (iGc < boxLeftEdgeGPos) {
          // op partially on screen only
          skipOpBases = static_cast<size_t> (
              boxLeftEdgeGPos - iGc
          );  // +ve
          iQuery += skipOpBases;
          iRef += skipOpBases;
          opLenRemain -= skipOpBases;
        }

        // set bases
        // masking bases that match the reference as '='.
        for (size_t i = 0; i < opLenRemain &&
                           writeHead.x < last (queryBox.xspan);
             ++i) {
          uintattr_t dispAttr = 0;
          auto dispChar = rawSeq[iQuery + i];
          if (ref && (dispChar == (*ref)[iRef + i])) {
            dispChar = '=';
            dispAttr = TB_DIM;
          }
          set (
              writeHead, static_cast<uint32_t> (dispChar),
              dispAttr
          );
          ++writeHead.x;
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
        size_t skipOpBases =
            (iGc < boxLeftEdgeGPos)
                ? static_cast<size_t> (boxLeftEdgeGPos - iGc)
                : 0;
        for (size_t i = skipOpBases;
             i < opSz && writeHead.x < last (queryBox.xspan);
             ++i) {
          set (writeHead, '-');
          ++writeHead.x;
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
        const auto anchorCell = writeHead - e2::dX (1);
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
        const int labSz = static_cast<int> (clipLabel.size());
        if (startToLEdge < labSz) {
          clipLabel = clipLabel.substr (
              static_cast<size_t> (labSz - startToLEdge)
          );
        }
        const auto drawnSz = static_cast<int> (clipLabel.size());
        e2::write_ascii_string (
            writeHead - e2::dX (drawnSz), last (queryBox.xspan),
            clipLabel, TB_DIM
        );
      }
      if (opType == BAM_CSOFT_CLIP && iOp == (nCig - 1)) {
        // soft clipping at end of read
        std::string clipLabel =
            "s(" + std::to_string (opSz) + ")";
        // if no space left, no-op
        e2::write_ascii_string (
            writeHead, last (queryBox.xspan), clipLabel, TB_DIM
        );
      }

      iQuery += opSz;
    }
    else {
      // hard clip/padding not handled
      continue;
    }

    if (writeHead.x >= last (queryBox.xspan)) {
      // early exit if row exhausted
      break;
    }
  }
}

static VoidOrErr draw_query_data (
    BrowserWgt& bWgt, DynamicSelectReadsStmt& stmt,
    const PileupDB& db, const LocusData& locus,
    const DataColList& displayCols
)
{
  // draw reads and data table

  sqlite3_reset (stmt);

  auto& qBox = bWgt.queryBox;
  auto& dBox = bWgt.dataBox;
  auto& hdrLine = bWgt.tableHeaderLine;

  draw_data_table_header (hdrLine, displayCols);

  auto nRow = height (qBox);

  int iRead = 0;
  int iRow = 0;
  for (; iRow < nRow; iRead++) {
    // NOTE: crash?
    auto nrRet = next_read (stmt, db);
    if (!nrRet) {
      return std::unexpected{nrRet.error()};
    }
    if (!(*nrRet)) {
      break;  // reads exhausted
    }
    if (iRead < bWgt.rowStart) {
      continue;  // scrolling
    }
    draw_sequence (qBox, iRow, stmt, locus);
    draw_data_table_row (dBox, iRow, stmt, displayCols);
    ++iRow;
  }

  auto pileupXPos = first (qBox.xspan) + (width (qBox) / 2);
  add_attr (e2::VLine{pileupXPos, qBox.yspan}, TB_REVERSE);
  set (
      e2::GlobalCell{pileupXPos, first (qBox.yspan) - 1}, '|',
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
        {first (pWgt.refLine.xspan) + proj.xOffset,
         pWgt.refLine.y},
        last (pWgt.refLine.xspan),
        locusData.refSlice->substr (proj.skipChars)
    );
  }

  // locus info
  {
    auto writeHead = first (pWgt.infoLine);
    const auto lineEnd = last (pWgt.infoLine.xspan);
    writeHead.x++;  // initial space
    writeHead.x += e2::write_ascii_string (
        writeHead, lineEnd, "LOCUS:", TB_DIM
    );
    writeHead.x++;  // space
    writeHead.x += e2::write_ascii_string (
        writeHead, lineEnd,
        fmt::format ("{}:{}", locusData.contig, locusData.pos)
    );
    writeHead.x++;  // space
    set (writeHead, boxch::vertLine, TB_DIM);
    writeHead.x += 2;  // past bar, then space
    writeHead.x += e2::write_ascii_string (
        writeHead, lineEnd, "SPAN:", TB_DIM
    );
    writeHead.x++;  // space
    writeHead.x += e2::write_ascii_string (
        writeHead, lineEnd,
        fmt::format ("{}-{}", locusData.start, locusData.end)
    );
    writeHead.x++;  // space
    set (writeHead, boxch::vertLine, TB_DIM);
  }
}

static VoidOrErr draw_piluep (
    BrowserWgt& pWgt, DynamicSelectReadsStmt& stmt,
    const PileupDB& db, const LocusData& locus,
    const DataColList& displayCols
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


static void draw_cmd (
    CmdWgt& cWgt, const DynamicFragments& userQuery
)
{
  e2::write_ascii_string (
      first (cWgt.inputLine), last (cWgt.inputLine).x,
      cWgt.inputBuf.text
  );
  auto cursorCell =
      first (cWgt.inputLine) +
      e2::dX (static_cast<int> (cWgt.inputBuf.curs));
  if (cursorCell.x < last (cWgt.inputLine).x) {
    e2::add_attr (cursorCell, TB_REVERSE);
  }
  e2::write_ascii_string (
      first (cWgt.msgLine), last (cWgt.msgLine).x, cWgt.msgBuf,
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
      last (cWgt.queryStatusLine).x, userClauseString, TB_DIM
  );
}

VoidOrErr draw_main_ui (
    UIBundle& ui, DBBundle& db, const DataColList& colsRequested
)
{
  // NOTE: set order does matter,
  // since some places just overwrite
  // previous draw calls
  PLOGD << "Drawing widgets";


  draw_layout_chrome (ui.browsr, ui.cmd);

  auto dpRet = draw_piluep (
      ui.browsr, db.stmt, db.db, db.locus, colsRequested
  );
  if (!dpRet) {
    // TODO: not really well thought out error handling.
    return std::unexpected (dpRet.error());
  }

  draw_cmd (ui.cmd, db.userClause);

  return {};
}
