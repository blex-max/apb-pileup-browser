#include "widgets.hpp"

#include <fmt/format.h>
#include <plog/Log.h>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

#include "app/state_components.hpp"
#include "backend/PileupDB.hpp"
#include "backend/sql.hpp"
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

void size_and_set_overlay_widget (
    UIBundle& ui, TextBlockRef content
)
{
  // set overlay widget, dynamically sizing to content
  assert (!content.empty());

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

  const auto maxLineW = std::ranges::max_element (
                            content, {}, &std::string_view::size
  )
                            ->size();
  // +2 for the left/right border, +1 for a gap before the
  // right border
  const auto framedContentW = static_cast<int> (maxLineW + 3);
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

VoidOrErr size_widgets (UIBundle& ui)
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

  // dynamically sized to content
  size_and_set_overlay_widget (ui, ui.help.content);

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

  set (body (bWgt.ambientSep), boxch::horzLine, TB_DIM);
  set (first (bWgt.ambientSep), boxch::rightTConnect, TB_DIM);
  set (last (bWgt.ambientSep), boxch::leftTConnect, TB_DIM);

  set (last (bWgt.headerSep), boxch::leftTConnect, TB_DIM);
}

static void draw_cmd_chrome (CmdWgt& cWgt)
{
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

static void draw_layout_chrome (BrowserWgt& bWgt, CmdWgt& cWgt)
{
  PLOGD << "Drawing layout";

  draw_browser_chrome (bWgt);
  draw_cmd_chrome (cWgt);
}

// --- end draw shared layout --- //

// --- draw browser pane --- //

namespace data_table {

static void draw_header (
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

static void draw_row_separators (
    e2::Box dataPane, const std::span<const ColMetadata*> cols
)
{
  auto writeHead = vertexA (dataPane);
  // exclusive limits
  const auto writeLimits =
      vertexC (dataPane) + e2::Delta{.dx = 1, .dy = 1};
  for (const auto* col : cols) {
    writeHead.x += static_cast<int> (col->displayWidth);
    if (writeHead.x > writeLimits.x) {
      break;
    }
    set (
        e2::VLine{
            .x = writeHead.x,
            .yspan = {writeHead.y, writeLimits.y}
        },
        boxch::vertLine
    );
    writeHead.x++;
  }
}

static void draw_row (
    e2::GlobalCell writeHead, int xLim, sqlite3_stmt* br_dbRow,
    const std::span<const ColMetadata*> cols
)
{
  assert (writeHead.x < xLim);

  std::string cellText{};
  for (const auto* col : cols) {
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
    e2::write_string (writeHead, xLim, cellText);
    writeHead.x += static_cast<int> (fieldWidth);
    // jump the field separator
    writeHead.x++;
    if (writeHead.x > xLim) {
      break;
    }
  }
}

}  // namespace data_table

namespace draw_alignment {


struct SharedArgs {
  const int16_t writeStartX;
  const int64_t writeStartXGPos;
  const int64_t pileupSpanGStart;
  const e2::GlobalCell writeLimits;
};

static SharedArgs prepare_shared (
    int16_t writeStartX, int64_t writeStartXGPos,
    int64_t pileupSpanGStart, const e2::GlobalCell& writeLimits
)
{
  assert (pileupSpanGStart > 0);
  assert (writeStartXGPos > 0);
  assert (valid (writeLimits));

  return SharedArgs{
      .writeStartX = writeStartX,
      .writeStartXGPos = writeStartXGPos,
      .pileupSpanGStart = pileupSpanGStart,
      .writeLimits = writeLimits
  };
}


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

struct Seq1Switches {
  bool drawQualTrack;
  bool drawInsTrack;
};
// draw aligned data to seq pane
static e2::Delta seq1 (
    const int16_t yStart, sqlite3_stmt* br_dbRow,
    const std::optional<std::string>& ref, const SharedArgs& sh,
    const Seq1Switches& switches
)
{
  constexpr uint8_t consumesNeitherQueryOrRef = 0b00;
  constexpr uint8_t consumesQueryOnly = 0b01;
  constexpr uint8_t consumesRefOnly = 0b10;
  constexpr uint8_t consumesQueryAndRef = 0b11;

  if (sh.writeStartX >= sh.writeLimits.x ||
      yStart >= sh.writeLimits.y) {
    return {0, 0};  // no-op
  }

  e2::GlobalCell writeHead{{.x = sh.writeStartX, .y = yStart}};

  // ordered offsets for optional tracks
  constexpr int trackYOffsetBase = 1;
  const int trackYOffsetIns = trackYOffsetBase;  // first
  const int trackYOffsetQual =
      trackYOffsetBase +
      static_cast<int> (switches.drawInsTrack);
  const int trackYOffsetInsQual =
      trackYOffsetBase +
      static_cast<int> (switches.drawInsTrack) +
      static_cast<int> (switches.drawQualTrack);

  bool enableInsTrack =
      switches.drawInsTrack &&
      (writeHead.y + trackYOffsetIns) < sh.writeLimits.y;
  bool enableQualTrack =
      switches.drawQualTrack &&
      (writeHead.y + trackYOffsetQual) < sh.writeLimits.y;

  const auto readFields = get_seq1_read_fields (br_dbRow);

  // NOTE: since view is centered on pileup,
  // all reads should always be at least partially in view
  // (unless pane is folded)
  const int startToLEdge =
      static_cast<int> (readFields.rStart - sh.writeStartXGPos);
  if (startToLEdge > 0) {
    writeHead.x += startToLEdge;
  }

  auto iGc = readFields.rStart;  // current genomic coordinate
  size_t iQuery = 0;
  // locus start always <= readFields.rStart
  size_t iRef = ref ? static_cast<size_t> (
                          readFields.rStart - sh.pileupSpanGStart
                      )
                    : 0;
  // track any insertion displayed on screen
  bool readInsDrawn = false;
  // buffer for subsequently writing quality string
  std::string qualDisplayBuf (
      static_cast<size_t> (sh.writeLimits.x - sh.writeStartX),
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
    const auto opOnScreen = (iGc + opSz) >= sh.writeStartXGPos;

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
        (iGc < sh.writeStartXGPos)
            ? static_cast<size_t> (sh.writeStartXGPos - iGc)
            : 0;

    if (opConsumeType == consumesQueryOnly) {
      const auto opType = bam_cigar_op (op);

      if (opType == BAM_CINS && iGc > sh.writeStartXGPos) {
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
          if (opInsWriteHead.x < sh.writeLimits.x) {
            e2::set (opInsWriteHead, '^', TB_DIM);
            opInsWriteHead.x++;
          }
          for (size_t i = 0;
               i < opSz && opInsWriteHead.x < sh.writeLimits.x;
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
          if (opInsQualWriteHead.x < sh.writeLimits.x) {
            e2::set (opInsQualWriteHead, '^', TB_DIM);
            opInsQualWriteHead.x++;
          }
          for (size_t i = 0; i < opSz && opInsQualWriteHead.x <
                                             sh.writeLimits.x;
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
            writeHead - e2::dX (drawnSz), sh.writeLimits.x,
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
            writeHead, sh.writeLimits.x, clipLabel, TB_DIM
        );
      }

      iQuery += opSz;
    }
    else if (opConsumeType == consumesRefOnly) {
      // Deletions, or "skipped region"
      //     - the latter only relevant to RNA (TODO: disambiguate?)
      // don't advance query tracker

      for (size_t i = skipOffscreenBases;
           i < opSz && writeHead.x < sh.writeLimits.x;
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
           i < opLenRemain && writeHead.x < sh.writeLimits.x;
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
              static_cast<int16_t> (writeHead.x) - sh.writeStartX
          )] = readFields.qual[iQuery + i];
        }
      }

      iQuery += opLenRemain;
      iRef += opLenRemain;
      iGc += opSz;
    }

    if (writeHead.x >= sh.writeLimits.x) {
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
        e2::GlobalCell{{.x = sh.writeStartX, .y = writeHead.y}},
        sh.writeLimits.x, qualDisplayBuf, TB_DIM
    );
    writeHead.y++;
  }
  if (enableQualTrack && readInsDrawn) {
    writeHead.y++;
  }

  return {
      .dx = writeHead.x - sh.writeStartX,
      .dy = writeHead.y - yStart
  };
}

}  // namespace draw_alignment

static VoidOrErr draw_query_data (
    BrowserWgt& bWgt, DBBundle& db, const AppConfig& conf
)
{
  // draw reads and data table
  PLOGD << "Drawing browser child panes";

  // preconditions:
  assert (valid (bWgt.frame));
  assert (size (bWgt.frame.xspan) > 2);

  sqlite3_reset (db.stmt);

  uint16_t tableWidth = 0;
  std::vector<const ColMetadata*> activeCols;
  for (const auto& col : conf.displayTableCols) {
    if (col.visible) {
      activeCols.emplace_back (&col);
      // +1 per column for the field separator drawn after it
      // (see data_table::draw_header/draw_row_separators/draw_row)
      tableWidth += col.displayWidth + 1;
    }
  }
  // Reserve room for at least one column of alignment/pileup
  // view, so a narrow terminal shrinks the table pane rather
  // than squeezing the alignment pane out of existence. (The
  // vSep column is already accounted for below: the table
  // pane's real rendered width is tableWidth - 1.)
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

    data_table::draw_header (
        bWgt.tablePaneHeaderLine, activeCols
    );
    data_table::draw_row_separators (
        bWgt.tablePaneDataBox, activeCols
    );
    bWgt.vSep = {
        splitAbsX,
        construct_relative (frameY, 0, size (frameY) - 1)
    };
    set (body (bWgt.vSep), boxch::vertLine, TB_DIM);
    set (first (bWgt.vSep), boxch::downTConnect, TB_DIM);
    set (last (bWgt.vSep), boxch::upTConnect, TB_DIM);
  }
  else {
    bWgt.tablePaneHeaderLine = {};  // invalid
    bWgt.tablePaneDataBox = {};
    bWgt.vSep = {};

    bWgt.alnPaneRefLine = {contentX, first (contentY)};
    bWgt.alnPaneDataBox = {contentX, dataY};
  }

  auto seqWriteHead = vertexA (bWgt.alnPaneDataBox);
  auto seqWriteLim = vertexC (bWgt.alnPaneDataBox) +
                     e2::dXY (1, 1);  // exclusive limit

  const auto drawAlignmentShared =
      draw_alignment::prepare_shared (
          seqWriteHead.x,
          db.locus.pos - (width (bWgt.alnPaneDataBox) / 2),
          db.locus.start, seqWriteLim
      );

  uint16_t nReadDrawn = 0;
  for (uint16_t iRead = 0; seqWriteHead.y < seqWriteLim.y;
       ++iRead) {
    auto nrRet = next_read (db.stmt, db.db);
    if (!nrRet) {
      // poor error handling policy
      return std::unexpected{nrRet.error()};
    }
    if (!(*nrRet)) {
      break;  // reads exhausted
    }
    if (static_cast<int64_t> (iRead) < db.stmtRowScrollOffset) {
      // reads hidden by scrolling
      continue;
    }
    const auto dHead = draw_alignment::seq1 (
        seqWriteHead.y, db.stmt, db.locus.refSlice,
        drawAlignmentShared,
        draw_alignment::Seq1Switches{
            conf.drawTrackSwitches.qual,
            conf.drawTrackSwitches.ins
        }
    );
    if (conf.drawPaneSwitches.table) {
      data_table::draw_row (
          e2::GlobalCell{
              {.x = first (bWgt.tablePaneDataBox.xspan),
               .y = seqWriteHead.y}
          },
          last (bWgt.tablePaneDataBox.xspan), db.stmt, activeCols
      );
    }
    seqWriteHead.y += dHead.dy;
    ++nReadDrawn;
  }
  bWgt.nReadOnscreen = nReadDrawn;

  auto pileupXPos = first (bWgt.alnPaneDataBox.xspan) +
                    (width (bWgt.alnPaneDataBox) / 2);

  e2::VLine pileupCrosshair{
      pileupXPos, bWgt.alnPaneDataBox.yspan
  };
  // At some point I thought it was necessary to
  // rm the DIM attribute under the crosshair because
  // something looked bad. I can't reproduce that
  // now so leaving the attr.
  // rm_attr (pileupCrosshair, TB_DIM);
  add_attr (pileupCrosshair, TB_REVERSE);
  // connect to ref base
  set (
      e2::GlobalCell{
          pileupXPos, first (bWgt.alnPaneDataBox.yspan) - 1
      },
      '|', TB_DIM
  );

  return {};
}

static void draw_pileup_ambient (
    BrowserWgt& pWgt, const PileupMetadata& locusData
)
{
  // TODO: get rid of projection function (?)
  if (locusData.refSlice) {
    auto proj = align_seq_to_box (
        locusData.pos, size (pWgt.alnPaneRefLine),
        locusData.start
    );

    e2::write_string (
        {first (pWgt.alnPaneRefLine.xspan) + proj.xOffset,
         pWgt.alnPaneRefLine.y},
        last (pWgt.alnPaneRefLine.xspan),
        locusData.refSlice->substr (proj.skipChars)
    );
  }

  // locus info
  {
    auto writeHead = first (pWgt.ambientLine);
    const auto lineEnd = last (pWgt.ambientLine.xspan);
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

static VoidOrErr draw_piluep (
    BrowserWgt& pWgt, DBBundle& db, const AppConfig& conf
)
{
  auto dqRet = draw_query_data (pWgt, db, conf);
  if (!dqRet) {
    return std::unexpected (dqRet.error());
  }

  draw_pileup_ambient (pWgt, db.locus);

  return {};
}

// --- end draw browser pane --- //


static void draw_cmd (
    CmdWgt& cWgt, const DynamicFragments& userQuery
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

VoidOrErr draw_main_ui (
    UIBundle& ui, DBBundle& db, const AppConfig& conf
)
{
  // NOTE: set order does matter,
  // since some places just overwrite
  // previous draw calls
  PLOGD << "Drawing widgets";

  draw_layout_chrome (ui.browsr, ui.cmd);

  auto dpRet = draw_piluep (ui.browsr, db, conf);
  if (!dpRet) {
    // TODO: not really well thought out error handling.
    return std::unexpected (dpRet.error());
  }

  draw_cmd (ui.cmd, db.userClause);

  return {};
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
