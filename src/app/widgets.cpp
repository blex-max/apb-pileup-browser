#include "widgets.hpp"

#include <fmt/format.h>
#include <plog/Log.h>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>

#include "app/state_components.hpp"
#include "backend/hts_sql.hpp"
#include "backend/schema.hpp"
#include "frontend/drawing_chars.hpp"
#include "frontend/extb/box/box.hpp"
#include "frontend/extb/extb.hpp"

// --- helpers --- //

namespace validate {

// slightly pointless,
// but I think it's more clear as to the
// intention than raw calls to valid (wgt.frame),
// and easier to add any future validation conditions.
static bool widget_is_valid (const BrowserWgt& bWgt)
{
  return valid (bWgt.frame);
};
static bool widget_is_valid (const CmdWgt& cWgt)
{
  return valid (cWgt.frame);
};
static bool widget_is_valid (const OverlayWgt& oWgt)
{
  return valid (oWgt.frame);
}
static bool ui_is_valid (const UIBundle& ui)
{
  return ui.screenW > 0 && ui.screenH > 0 &&
         widget_is_valid (ui.browsr) &&
         widget_is_valid (ui.cmd) &&
         widget_is_valid (ui.overlay);
}

}  // namespace validate

// --- end helpers --- //

// --- size calculation --- //

bool size_and_set_overlay_widget (
    OverlayWgt& oWgt, helpblocks::TextBlockRef content,
    int screenW, int screenH
)
{
  /* set overlay widget, dynamically sizing to content */
  assert (screenW > 0);
  assert (screenH > 0);
  assert (!content.empty());

  // dynamically sized to content
  const auto framedContentH =
      static_cast<int> (content.size() + 2);
  const auto helpH = std::min<int> (
      framedContentH, static_cast<int> (std::ceil (
                          static_cast<double> (screenH) * 0.6
                      ))
  );

  const auto maxLineW = std::ranges::max_element (
                            content, {}, &std::string_view::size
  )
                            ->size();
  // +2 for the left/right border, +1 for a gap before the
  // right border
  const auto framedContentW = static_cast<int> (maxLineW + 3);
  const auto wgtW = std::min (
      framedContentW, static_cast<int> (std::ceil (
                          static_cast<double> (screenW) * 0.6
                      ))
  );
  if (wgtW < 5) {
    // too small
    return false;
  }

  const auto xOff =
      static_cast<int> (std::floor ((screenW - wgtW) / 2));
  const auto yOff =
      static_cast<int> (std::floor ((screenH - helpH) / 2));

  e2::Span ySpan{yOff, yOff + helpH};
  e2::Span xSpan{xOff, xOff + wgtW};

  oWgt.frame = e2::Box{xSpan, ySpan};
  oWgt.contentBox = e2::Box{body (xSpan), body (ySpan)};
  oWgt.content = content;

  return true;
}

bool size_widgets (UIBundle& ui)
{
  PLOGD << "Calculating widget size";

  const auto screenW = ui.screenW = tb_width();
  const auto screenH = ui.screenH = tb_height();

  const e2::Span screenX{0, screenW};
  const e2::Span screenY{0, screenH};

  // vertical sectioning of terminal
  const e2::Span mainY{
      screenY.first, screenY.last - CmdWgt::widgetHeight
  };
  const e2::Span cmdY{
      mainY.last, mainY.last + CmdWgt::widgetHeight
  };

  PLOGD << "screen y last: " << screenY.last;
  PLOGD << "cmd y first: " << cmdY.first;
  PLOGD << "cmd y last: " << cmdY.last;

  if (!e2::valid (screenY) || !e2::valid (screenX) ||
      !e2::valid (mainY) || !e2::valid (cmdY)) {
    return false;
  }

  {
    auto& bWgt = ui.browsr;
    bWgt.frame = e2::Box{screenX, mainY};
    const auto& browsrContentX = body (screenX);
    const auto& browsrContentY = body (mainY);
    // skip header, separator. leave 2 rows at end
    const auto dataY = e2::Span{
        first (browsrContentY) + 2, last (browsrContentY) - 1
    };
    bWgt.headerSep = {
        screenX, first (browsrContentY) + 1
    };  // overlapping frame in X, to set connectors
    bWgt.ambientSep = {screenX, last (dataY)};
    bWgt.ambientLine = {browsrContentX, last (browsrContentY)};
  }

  {
    auto& cWgt = ui.cmd;
    cWgt.frame = e2::Box{screenX, cmdY};

    auto y = first (cmdY) + 1;
    cWgt.queryStatusLine = e2::HLine{body (screenX), y++};
    cWgt.statusSep = e2::HLine{
        screenX,  // include frame, to draw pipe connectors at line ends
        y++
    };

    // cmd input
    cWgt.inputCaret = e2::GlobalCell{first (screenX) + 1, y};
    cWgt.inputLine = e2::HLine{
        // skip border, leave space for caret ':'
        construct_relative (screenX, 2, size (screenX) - 1), y++
    };

    cWgt.sepLine = e2::HLine{
        screenX,  // include frame, to draw pipe connectors at line ends
        y++
    };

    cWgt.msgLine = e2::HLine{body (screenX), y};
  }

  // dynamically sized to content and
  // current screen size.
  assert (!ui.overlay.content.empty());
  if (!size_and_set_overlay_widget (
          ui.overlay, ui.overlay.content, screenW, screenH
      )) {
    return false;
  };
  return true;
}
// --- end sizing --- //

// --- draw layout chrome --- //

static void draw_browser_chrome (BrowserWgt& bWgt)
{
  // preconds
  assert (valid (bWgt.frame));
  assert (width (bWgt.frame) > 1);
  assert (height (bWgt.frame) > 1);

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

  set (body (bWgt.ambientSep), boxch::horzLine, TB_DIM);
  set (first (bWgt.ambientSep), boxch::rightTConnect, TB_DIM);
  set (last (bWgt.ambientSep), boxch::leftTConnect, TB_DIM);

  set (last (bWgt.headerSep), boxch::leftTConnect, TB_DIM);
}

static void draw_cmd_chrome (CmdWgt& cWgt)
{
  // preconds
  assert (valid (cWgt.frame));
  assert (width (cWgt.frame) > 1);
  assert (height (cWgt.frame) > 1);

  auto& cFrame = cWgt.frame;
  set (vertexA (cFrame), boxch::topLeftRoundCorner, TB_DIM);
  set (vertexB (cFrame), boxch::topRightRoundCorner, TB_DIM);
  set (vertexD (cFrame), boxch::bottomLeftRoundCorner, TB_DIM);
  set (vertexC (cFrame), boxch::bottomRightRoundCorner, TB_DIM);

  set (body (edgeAB (cFrame)), boxch::horzHeavy, TB_DIM);
  set (body (edgeDA (cFrame)), boxch::vertLine, TB_DIM);
  set (body (edgeBC (cFrame)), boxch::vertLine, TB_DIM);
  set (body (cWgt.statusSep), boxch::horzLine, TB_DIM);

  set (cWgt.inputCaret, ':');
  set (body (cWgt.sepLine), boxch::horzLine, TB_DIM);
}

// --- end draw chrome --- //

// --- draw browser pane --- //

namespace draw_table {

static void header (
    const e2::HLine& headerLine,
    const std::span<const ColMetadata*> cols
)
{
  PLOGD << "Drawing table header";

  assert (valid (headerLine));

  const int xLim = last (headerLine.xspan);
  e2::GlobalCell writeHead{
      {.x = first (headerLine.xspan), .y = headerLine.y}
  };
  std::string lpb_assembled;  // lpb_ loop buffer
  for (const auto* col : cols) {
    // assemble field into "centered" title string
    // write and advance
    uint16_t padL = 0;
    uint16_t padR = 0;
    const auto fieldWidth = static_cast<int> (col->displayWidth);
    const auto fieldName = col->fieldName;
    const auto fieldNameLen =
        static_cast<int> (fieldName.size());
    if (fieldNameLen < fieldWidth) {
      padL = static_cast<uint16_t> (
          (fieldWidth - fieldNameLen) / 2
      );
      padR = static_cast<uint16_t> (fieldWidth - fieldNameLen) -
             padL;
    }
    lpb_assembled = std::string (padL, ' ') +
                    std::string (fieldName) +
                    std::string (padR, ' ');
    // will clip if too long
    e2::write_string (writeHead, xLim, lpb_assembled);
    writeHead.x += fieldWidth;
    if (writeHead.x > xLim) {
      break;
    }
    set (writeHead, boxch::vertLine);
    writeHead.x++;
  }
}

static void row_separators (
    e2::Box dataPane, const std::span<const ColMetadata*> cols
)
{
  auto writeHead = vertexA (dataPane);
  // exclusive limits
  const auto writeXLimit = last (dataPane.xspan);
  const auto paneYLimit = last (dataPane.yspan);
  for (const auto* col : cols) {
    writeHead.x += static_cast<int> (col->displayWidth);
    if (writeHead.x > writeXLimit) {
      break;
    }
    set (
        e2::VLine{
            .x = writeHead.x, .yspan = {writeHead.y, paneYLimit}
        },
        boxch::vertLine
    );
    writeHead.x++;
  }
}

struct Row1FixedArgs {
  int writeXStart;
  int writeXLimit;
  const std::span<const ColMetadata*> cols;

  bool valid() const noexcept
  {
    // check before calling row1
    return writeXStart <= writeXLimit;
  }
};
static void row1 (
    int writeY, sqlite3_stmt* br_dbRow, Row1FixedArgs fa
)
{
  auto writeX = fa.writeXStart;

  std::string cellText{};
  for (const auto* col : fa.cols) {
    cellText = col->fn_retrieve_from_db (br_dbRow);
    const auto fieldWidth = col->displayWidth;
    if (cellText.size() > fieldWidth) {
      // shrink to fit
      cellText.resize (fieldWidth);
    }
    else {
      // pad
      cellText.resize (fieldWidth, ' ');
    }
    e2::write_string (
        {writeX, writeY}, fa.writeXLimit, cellText
    );
    writeX += static_cast<int> (fieldWidth);
    // jump the field separator
    writeX++;
    if (writeX > fa.writeXLimit) {
      break;
    }
  }
}

}  // namespace draw_table

namespace draw_aln {

namespace {
// seq1 internals

struct ReadFields {
  int64_t rStart;
  std::string seq;
  std::string qual;
  const uint32_t* cig_br;
  uint64_t nCig;
};
ReadFields get_seq1_read_fields (sqlite3_stmt* row)
{
  // NOTE: currently does no error checking
  return {
      .rStart =
          sqlite3_column_int64 (row, schema::FieldIndex::rstart),
      .seq =
          [&row]() {
            const auto* p = sqlite3_column_text (
                row, schema::FieldIndex::seq
            );
            const auto len = sqlite3_column_bytes (
                row, schema::FieldIndex::seq
            );
            return std::string (
                reinterpret_cast<const char*> (p),
                static_cast<size_t> (len)
            );
          }(),
      .qual =
          [&row]() {
            const auto* br_p = sqlite3_column_text (
                row, schema::FieldIndex::qual
            );
            const auto len = sqlite3_column_bytes (
                row, schema::FieldIndex::qual
            );
            return std::string (
                reinterpret_cast<const char*> (br_p),
                static_cast<size_t> (len)
            );
          }(),
      .cig_br =
          [&row]() {
            return static_cast<const uint32_t*> (
                sqlite3_column_blob (
                    row, schema::FieldIndex::cig_uint32
                )
            );
          }(),
      .nCig =
          [&row]() {
            return static_cast<uint64_t> (sqlite3_column_int (
                row, schema::FieldIndex::ncig
            ));
          }()
  };
};

}  // namespace

struct Seq1FixedArgs {
  const int16_t writeStartX;
  const int64_t writeStartXGPos;
  const int64_t pileupSpanGStart;
  const e2::GlobalCell writeLimits;
  bool drawQualTrack;
  bool drawInsTrack;

  bool valid() const noexcept
  {
    return writeStartX >= 0 && writeStartXGPos >= 0 &&
           pileupSpanGStart >= 0 &&
           writeStartXGPos >= pileupSpanGStart &&
           e2::valid (writeLimits);
  }
};
static e2::Delta seq1 (
    const int16_t yStart, sqlite3_stmt* br_dbRow,
    const std::optional<std::string>& ref,
    const Seq1FixedArgs& fa
)
{
  /* draw aligned data to aln pane */

  constexpr uint8_t consumesNeitherQueryOrRef = 0b00;
  constexpr uint8_t consumesQueryOnly = 0b01;
  constexpr uint8_t consumesRefOnly = 0b10;
  constexpr uint8_t consumesQueryAndRef = 0b11;

  // preconditions
  if (fa.writeStartX >= fa.writeLimits.x ||
      yStart >= fa.writeLimits.y) {
    return {0, 0};  // no-op
  }
  assert ((ref) ? !ref.value().empty() : true);

  e2::GlobalCell writeHead{{.x = fa.writeStartX, .y = yStart}};

  // ordered offsets for optional tracks
  constexpr int trackYOffsetBase = 1;
  const int trackYOffsetIns = trackYOffsetBase;  // first
  const int trackYOffsetQual =
      trackYOffsetBase + static_cast<int> (fa.drawInsTrack);
  const int trackYOffsetInsQual =
      trackYOffsetBase + static_cast<int> (fa.drawInsTrack) +
      static_cast<int> (fa.drawQualTrack);

  bool enableInsTrack =
      fa.drawInsTrack &&
      (writeHead.y + trackYOffsetIns) < fa.writeLimits.y;
  bool enableQualTrack =
      fa.drawQualTrack &&
      (writeHead.y + trackYOffsetQual) < fa.writeLimits.y;

  const auto readFields = get_seq1_read_fields (br_dbRow);

  // NOTE: since view is centered on pileup,
  // all reads should always be at least partially in view
  // (unless pane is folded)
  const int startToLEdge =
      static_cast<int> (readFields.rStart - fa.writeStartXGPos);
  if (startToLEdge > 0) {
    writeHead.x += startToLEdge;
  }

  auto iGc = readFields.rStart;  // current genomic coordinate
  size_t iQuery = 0;
  // locus start always <= readFields.rStart
  size_t iRef = ref ? static_cast<size_t> (
                          readFields.rStart - fa.pileupSpanGStart
                      )
                    : 0;
  // track any insertion displayed on screen
  bool readInsDrawn = false;
  // buffer for subsequently writing quality string
  std::string qualDisplayBuf (
      static_cast<size_t> (fa.writeLimits.x - fa.writeStartX),
      ' '
  );
  // FOR EACH OP
  for (size_t iOp = 0; iOp < readFields.nCig; iOp++) {
    const auto op = readFields.cig_br[iOp];
    const auto opConsumeType = bam_cigar_type (op);

    if (opConsumeType == consumesNeitherQueryOrRef) {
      // padding and hard clipping ignored
      continue;
    }

    const auto opSz = bam_cigar_oplen (op);
    const auto opOnScreen = (iGc + opSz) >= fa.writeStartXGPos;

    if (!opOnScreen) {
      // skip op, incrementing indexes
      switch (opConsumeType) {
        case (consumesQueryOnly):
          iQuery += opSz;
          continue;
        case (consumesRefOnly):
          iRef += opSz;
          iGc += opSz;
          continue;
        case (consumesQueryAndRef):
          iQuery += opSz;
          iRef += opSz;
          iGc += opSz;
          continue;
        default:
          break;
      }
    }

    // At least partially on screen, to be processed
    const auto skipOffscreenBases =
        (iGc < fa.writeStartXGPos)
            ? static_cast<size_t> (fa.writeStartXGPos - iGc)
            : 0;

    if (opConsumeType == consumesQueryOnly) {
      const auto opType = bam_cigar_op (op);

      if (opType == BAM_CINS && iGc > fa.writeStartXGPos) {
        // insertion
        // where at least one base PRIOR
        // to the insertion is visible

        // modify anchor base
        const auto anchorCell = writeHead - e2::dX (1);
        e2::clear_attrs (anchorCell);
        e2::extend (anchorCell, markch::ringAbove);

        if (enableInsTrack) {
          auto opInsWriteHead =
              anchorCell + e2::dY (trackYOffsetIns);
          if (opInsWriteHead.x < fa.writeLimits.x) {
            e2::set (opInsWriteHead, '^', TB_DIM);
            opInsWriteHead.x++;
          }
          for (size_t i = 0;
               i < opSz && opInsWriteHead.x < fa.writeLimits.x;
               ++i, ++opInsWriteHead.x) {
            set (
                opInsWriteHead, static_cast<uint32_t> (
                                    readFields.seq[iQuery + i]
                                )
            );
          }
          readInsDrawn = true;
        }
        else {
          // extra highlight if bases not unfolded
          e2::set_attr (anchorCell, TB_UNDERLINE);
        }

        if (enableQualTrack) {
          auto opInsQualWriteHead =
              anchorCell + e2::dY (trackYOffsetInsQual);
          if (opInsQualWriteHead.x < fa.writeLimits.x) {
            e2::set (opInsQualWriteHead, '^', TB_DIM);
            opInsQualWriteHead.x++;
          }
          for (size_t i = 0; i < opSz && opInsQualWriteHead.x <
                                             fa.writeLimits.x;
               ++i, ++opInsQualWriteHead.x) {
            set (
                opInsQualWriteHead,
                static_cast<uint32_t> (
                    readFields.qual[iQuery + i]
                ),
                TB_DIM
            );
          }
        }
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
        e2::write_string (
            writeHead - e2::dX (drawnSz), fa.writeLimits.x,
            clipLabel, TB_DIM
        );
      }

      if (opType == BAM_CSOFT_CLIP &&
          iOp == (readFields.nCig - 1)) {
        // soft clipping at end of read
        std::string clipLabel =
            "s(" + std::to_string (opSz) + ")";
        // if no space left, no-op
        e2::write_string (
            writeHead, fa.writeLimits.x, clipLabel, TB_DIM
        );
      }

      iQuery += opSz;
    }
    else if (opConsumeType == consumesRefOnly) {
      // Deletions, or "skipped region"
      //     - the latter only relevant to RNA (TODO: disambiguate?)
      // don't advance query tracker

      for (size_t i = skipOffscreenBases;
           i < opSz && writeHead.x < fa.writeLimits.x;
           ++i, ++writeHead.x) {
        set (writeHead, 'x', TB_UNDERLINE);
      }
      iRef += opSz;
      iGc += opSz;
    }
    else if (opConsumeType == consumesQueryAndRef) {
      auto opLenRemain = opSz;
      iQuery += skipOffscreenBases;
      iRef += skipOffscreenBases;
      opLenRemain -= skipOffscreenBases;

      // draw tracks
      for (size_t i = 0;
           i < opLenRemain && writeHead.x < fa.writeLimits.x;
           ++i, ++writeHead.x) {
        uintattr_t dispAttr = 0;
        auto dispChar = readFields.seq[iQuery + i];
        // mask bases that match the reference with '='.
        if (ref &&
            (std::toupper (
                 static_cast<unsigned char> (dispChar)
             ) ==
             std::toupper (
                 static_cast<unsigned char> ((*ref)[iRef + i])
             ))) {
          dispChar = '=';
          dispAttr = TB_DIM;
        }
        set (
            writeHead, static_cast<uint32_t> (dispChar), dispAttr
        );
        if (enableQualTrack) {
          qualDisplayBuf[static_cast<uint16_t> (
              static_cast<int16_t> (writeHead.x) - fa.writeStartX
          )] = readFields.qual[iQuery + i];
        }
      }

      iQuery += opLenRemain;
      iRef += opLenRemain;
      iGc += opSz;
    }

    if (writeHead.x >= fa.writeLimits.x) {
      // early exit if row exhausted
      break;
    }
  }
  writeHead.y++;  // one row always written by this point
  if (enableInsTrack && readInsDrawn) {
    writeHead.y++;
  }
  if (enableQualTrack) {
    // NOTE: it might be easier if ALL the
    // tracks used buffers. The buffers could
    // be hoisted even. The buffer type would
    // need to be either {char, style} or
    // use RLE. writing operations could operate
    // on a struct of vectors like chars, styles.
    // Probably less performant but potenially useful
    // for correctness and readability in some cases.
    e2::write_string (
        e2::GlobalCell{{.x = fa.writeStartX, .y = writeHead.y}},
        fa.writeLimits.x, qualDisplayBuf, TB_DIM
    );
    writeHead.y++;
  }
  if (enableQualTrack && readInsDrawn) {
    writeHead.y++;
  }

  return {
      .dx = writeHead.x - fa.writeStartX,
      .dy = writeHead.y - yStart
  };
}

}  // namespace draw_aln

namespace draw_query_data {

static TuiStatus draw_query_data (
    BrowserWgt& bWgt, DBBundle& db, const AppConfig& conf
)
{
  // draw reads and data table
  PLOGD << "Drawing browser child panes";

  if (!valid (bWgt.frame) || size (bWgt.frame.xspan) < 4 ||
      size (bWgt.frame.yspan) < 8) {
    // bounds slightly approximate
    return {TuiStatus::insufficientSz, std::nullopt};
  }
  assert (db.locusInfo.valid());

  sqlite3_reset (db.stmt);

  /* size widgets */
  uint16_t tableWidth = 0;
  std::vector<const ColMetadata*> activeCols;
  for (const auto& col : conf.displayTableCols) {
    if (col.visible) {
      activeCols.emplace_back (&col);
      // +1 per column for the field separator drawn after it
      // (see draw_table::)
      tableWidth += col.displayWidth + 1;
    }
  }
  // Reserve room for at least one column of alignment/pileup
  // view.
  constexpr int minAlnPaneWidth = 1;
  const auto maxTableWidth = static_cast<uint16_t> (
      std::max (0, size (bWgt.frame.xspan) - 2 - minAlnPaneWidth)
  );
  tableWidth = std::min (tableWidth, maxTableWidth);

  const auto& [frameX, frameY] = spans (bWgt.frame);
  const auto& contentX = body (frameX);
  const auto& contentY = body (frameY);
  // skip header, separator. leave 2 rows at end
  const auto dataY =
      e2::Span{first (contentY) + 2, last (contentY) - 1};

  if (conf.drawPaneSwitches.table) {
    const int splitAbsX = last (contentX) - tableWidth;
    e2::Span alnPaneX{first (contentX), splitAbsX};
    e2::Span tablePaneX{splitAbsX + 1, last (contentX)};

    bWgt.alnPaneRefLine = {alnPaneX, first (contentY)};
    bWgt.tablePaneHeaderLine = {tablePaneX, first (contentY)};
    bWgt.alnPaneDataBox = {alnPaneX, dataY};
    bWgt.tablePaneDataBox = {tablePaneX, dataY};
    bWgt.vSep = {
        splitAbsX,
        construct_relative (frameY, 0, size (frameY) - 1)
    };

    assert (valid (bWgt.tablePaneHeaderLine));
    assert (size (bWgt.tablePaneHeaderLine) > 0);
    assert (valid (bWgt.tablePaneDataBox));
    assert (height (bWgt.tablePaneDataBox) > 0);
    assert (width (bWgt.tablePaneDataBox) > 0);
  }
  else {
    bWgt.tablePaneHeaderLine = {};  // invalid
    bWgt.tablePaneDataBox = {};
    bWgt.vSep = {};

    bWgt.alnPaneRefLine = {contentX, first (contentY)};
    bWgt.alnPaneDataBox = {contentX, dataY};
  }
  assert (valid (bWgt.alnPaneDataBox));
  assert (height (bWgt.alnPaneDataBox) > 0);
  assert (width (bWgt.alnPaneDataBox) > 0);
  /* end size widgets */

  /* configure/validate draw coordinates */
  const auto alnPaneHalfWidth = width (bWgt.alnPaneDataBox) / 2;
  assert (db.locusInfo.pos >= alnPaneHalfWidth);
  const auto marginToPileupStart = std::min (
      -(db.locusInfo.pos - alnPaneHalfWidth -
        db.locusInfo.start),
      0LL
  );
  const auto marginToPileupEnd = std::max (
      db.locusInfo.end - alnPaneHalfWidth - db.locusInfo.pos, 0LL
  );
  bWgt.userPanOffset = std::clamp (
      bWgt.userPanOffset, marginToPileupStart, marginToPileupEnd
  );
  const auto alnPaneLeftmostGPos =
      db.locusInfo.pos - alnPaneHalfWidth + bWgt.userPanOffset;
  assert (alnPaneLeftmostGPos >= 0);
  /* end coordinates */

  if (db.locusInfo.refSlice) {
    /* draw reference */
    const int64_t offsetToLocusStart =
        db.locusInfo.start - alnPaneLeftmostGPos;

    int64_t skipRefBases;
    int64_t startDrawX;
    if (offsetToLocusStart < 0) {
      skipRefBases = -offsetToLocusStart;
      startDrawX = 0;
    }
    else {
      skipRefBases = 0;
      startDrawX = offsetToLocusStart;
    }

    e2::write_string (
        {first (bWgt.alnPaneRefLine.xspan) +
             static_cast<int> (startDrawX),
         bWgt.alnPaneRefLine.y},
        last (bWgt.alnPaneRefLine.xspan),
        db.locusInfo.refSlice->substr (
            static_cast<size_t> (skipRefBases)
        )
    );
  }

  if (conf.drawPaneSwitches.table) {
    draw_table::header (bWgt.tablePaneHeaderLine, activeCols);
    draw_table::row_separators (
        bWgt.tablePaneDataBox, activeCols
    );
    /* draw pane separator */
    set (body (bWgt.vSep), boxch::vertLine, TB_DIM);
    set (first (bWgt.vSep), boxch::downTConnect, TB_DIM);
    set (last (bWgt.vSep), boxch::upTConnect, TB_DIM);
  }

  /* iteratively draw query data */
  auto seqWriteHead = vertexA (bWgt.alnPaneDataBox);
  auto seqWriteLim = vertexC (bWgt.alnPaneDataBox) +
                     e2::dXY (1, 1);  // exclusive limit
  const draw_aln::Seq1FixedArgs seq1Fixed{
      .writeStartX = static_cast<int16_t> (seqWriteHead.x),
      .writeStartXGPos = alnPaneLeftmostGPos,
      .pileupSpanGStart = db.locusInfo.start,
      .writeLimits = seqWriteLim,
      .drawInsTrack = conf.drawTrackSwitches.ins,
      .drawQualTrack = conf.drawTrackSwitches.qual,
  };
  assert (seq1Fixed.valid());
  const draw_table::Row1FixedArgs row1Fixed{
      .writeXStart = first (bWgt.tablePaneDataBox.xspan),
      .writeXLimit = last (bWgt.tablePaneDataBox.xspan),
      .cols = activeCols,
  };
  assert (row1Fixed.valid());
  if (db.nStmtRows > 0) {
    uint16_t nReadDrawn = 0;
    for (uint16_t iRead = 0; seqWriteHead.y < seqWriteLim.y;
         ++iRead) {
      const auto iterStatus = next_read (db.stmt);
      if (!iterStatus) {
        return {TuiStatus::sqlFail, iterStatus.error()};
      }
      if (*iterStatus == query::RowIterStatus::rowAvail) {
        if (static_cast<int64_t> (iRead) <
            db.stmtRowScrollOffset) {
          // reads hidden by scrolling
          continue;
        }
        const auto dHead = draw_aln::seq1 (
            static_cast<int16_t> (seqWriteHead.y), db.stmt,
            db.locusInfo.refSlice, seq1Fixed
        );
        if (conf.drawPaneSwitches.table) {
          draw_table::row1 (seqWriteHead.y, db.stmt, row1Fixed);
        }
        seqWriteHead.y += dHead.dy;
        ++nReadDrawn;
      }
      else {
        break;
      }
    }
    bWgt.nReadOnscreen = nReadDrawn;

    /* draw crosshair */
    if (const auto pileupScreenXPos = static_cast<int16_t> (
            first (bWgt.alnPaneDataBox.xspan) +
            alnPaneHalfWidth - bWgt.userPanOffset
        );
        pileupScreenXPos < last (bWgt.alnPaneDataBox.xspan)) {
      // if user has not scrolled crosshair offscreen:
      e2::VLine pileupCrosshair{
          pileupScreenXPos, bWgt.alnPaneDataBox.yspan
      };
      add_attr (pileupCrosshair, TB_REVERSE);
      // draw marker linking reference base and query position of pileup
      set (
          e2::GlobalCell{
              pileupScreenXPos,
              first (bWgt.alnPaneDataBox.yspan) - 1
          },
          '|', TB_DIM
      );
    }
    /* end draw crosshair */
  }
  else {
    e2::write_string (
        seqWriteHead, seqWriteLim.x,
        "no reads at locus for current query", TB_DIM
    );
  }
  /* end draw query data */

  return {TuiStatus::success, std::nullopt};
}

}  // namespace draw_query_data


static void draw_pileup_ambient (
    BrowserWgt& bWgt, const query::PileupMetadata& locusData
)
{
  assert (validate::widget_is_valid (bWgt));
  assert (locusData.valid());

  // locus info
  {
    auto writeHead = first (bWgt.ambientLine);
    const auto lineEnd = last (bWgt.ambientLine.xspan);
    writeHead.x++;  // initial space
    writeHead.x +=
        e2::write_string (writeHead, lineEnd, "LOCUS:", TB_DIM);
    writeHead.x++;  // space
    writeHead.x += e2::write_string (
        writeHead, lineEnd,
        fmt::format ("{}:{}", locusData.contig, locusData.pos)
    );
    writeHead.x++;  // space
    set (writeHead, boxch::vertLine, TB_DIM);
    writeHead.x += 2;  // past bar, then space
    writeHead.x +=
        e2::write_string (writeHead, lineEnd, "SPAN:", TB_DIM);
    writeHead.x++;  // space
    writeHead.x += e2::write_string (
        writeHead, lineEnd,
        fmt::format ("{}-{}", locusData.start, locusData.end)
    );
    writeHead.x++;  // space
    set (writeHead, boxch::vertLine, TB_DIM);
  }
}

// --- end draw browser pane --- //


static void draw_cmd (
    CmdWgt& cWgt, const query::DynamicFragments& userQuery
)
{
  e2::write_string (
      first (cWgt.inputLine), last (cWgt.inputLine).x,
      cWgt.inputBuf.text
  );
  auto cursorCell =
      first (cWgt.inputLine) +
      e2::dX (static_cast<int> (cWgt.inputBuf.curs));
  if (cursorCell.x < last (cWgt.inputLine).x) {
    e2::add_attr (cursorCell, TB_REVERSE);
  }
  e2::write_string (
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

  e2::write_string (
      first (cWgt.queryStatusLine),
      last (cWgt.queryStatusLine).x, userClauseString, TB_DIM
  );
}

TuiStatus draw_main_ui (
    UIBundle& ui, DBBundle& db, const AppConfig& conf
)
{
  // NOTE: set order does matter,
  // since some places just overwrite
  // previous draw calls
  PLOGD << "Drawing widgets";
  assert (validate::ui_is_valid (ui));

  draw_browser_chrome (ui.browsr);
  draw_cmd_chrome (ui.cmd);


  switch (
      const auto dqStatus =
          draw_query_data::draw_query_data (ui.browsr, db, conf);
      dqStatus.code
  ) {
    case TuiStatus::success:
      break;
    case TuiStatus::insufficientSz:
    case TuiStatus::sqlFail:
      return dqStatus;
  }

  draw_pileup_ambient (ui.browsr, db.locusInfo);

  draw_cmd (ui.cmd, db.userClause);

  return {};
}

void draw_overlay (const OverlayWgt& oWgt)
{
  assert (validate::widget_is_valid (oWgt));
  assert (!oWgt.content.empty());

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
  set (vertexD (frame), boxch::bottomLeftRoundCorner);
  set (vertexC (frame), boxch::bottomRightRoundCorner);

  auto xEnd =
      last (box.xspan) - 1;  // leave a gap before the border

  auto writeHead = vertexA (frame);
  writeHead.x += 1;
  writeHead.x += e2::write_string (
      writeHead, xEnd, " q: close overlay ", TB_DIM
  );
  writeHead.x += 3;
  const auto lnN = height (box);
  if (lnN < std::ssize (content)) {
    e2::write_string (
        writeHead, xEnd, "Up / Down: scroll", TB_DIM
    );
  }

  const auto lnOff = static_cast<size_t> (oWgt.contentLnOffset);
  auto lnY = extb::vertexA (box);
  for (int i = 0; i < lnN && i < std::ssize (content); ++i) {
    e2::write_string (
        lnY, xEnd, content[static_cast<size_t> (i) + lnOff]
    );
    ++lnY.y;
  }
}
