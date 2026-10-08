#include "app/widgets.hpp"

#include <fmt/format.h>
#include <htslib/sam.h>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>

#include "app/g_state.hpp"
#include "backend/hts_sql.hpp"
#include "backend/schema.hpp"
#include "frontend/drawing_chars.hpp"
#include "frontend/extb/box/box.hpp"
#include "frontend/extb/extb.hpp"
#include "shared/apb_assert.hpp"

namespace g_db = g_state::db;
namespace g_browsr = g_state::ui::browsr;
namespace g_cmd = g_state::ui::cmd;
namespace g_overlay = g_state::ui::overlay;
namespace g_ui = g_state::ui;

// --- helpers --- //

namespace validate {

// slightly pointless,
// but I think it's more clear as to the
// intention than raw calls to valid (wgt.frame),
// and easier to add any future validation conditions.
static bool browser_widget_is_valid() { return valid (g_browsr::frame); };
static bool cmd_widget_is_valid() { return valid (g_cmd::frame); };
static bool overlay_widget_is_valid() { return valid (g_overlay::frame); }
static bool ui_is_valid()
{
  return tb_width() > 0 && tb_height() > 0 && cmd_widget_is_valid() &&
         browser_widget_is_valid() && overlay_widget_is_valid();
}

}  // namespace validate

// --- end helpers --- //

// --- size calculation --- //

bool size_and_set_overlay_widget (helpblocks::TextBlockRef content)
{
  /* set overlay widget, dynamically sizing to content */
  const auto screenW = tb_width();
  const auto screenH = tb_height();
  APB_ASSERT (screenW > 0, screenW);
  APB_ASSERT (screenH > 0, screenH);
  APB_ASSERT (!content.empty());

  // dynamically sized to content
  const auto framedContentH = static_cast<int> (content.size() + 2);
  const auto helpH = std::min<int> (
      framedContentH,
      static_cast<int> (std::ceil (static_cast<double> (screenH) * 0.6))
  );

  const auto maxLineW =
      std::ranges::max_element (content, {}, &std::string_view::size)
          ->size();
  // +2 for the left/right border, +1 for a gap before the
  // right border
  const auto framedContentW = static_cast<int> (maxLineW + 3);
  const auto wgtW = std::min (
      framedContentW,
      static_cast<int> (std::ceil (static_cast<double> (screenW) * 0.6))
  );
  if (wgtW < 5) {
    // too small
    return false;
  }

  const auto xOff = static_cast<int> (std::floor ((screenW - wgtW) / 2));
  const auto yOff = static_cast<int> (std::floor ((screenH - helpH) / 2));

  e2::Span ySpan{yOff, yOff + helpH};
  e2::Span xSpan{xOff, xOff + wgtW};

  g_overlay::frame = e2::Box{xSpan, ySpan};
  g_overlay::contentBox = e2::Box{body (xSpan), body (ySpan)};
  g_overlay::content = content;

  return true;
}

bool size_widgets()
{
  const auto screenW = tb_width();
  const auto screenH = tb_height();

  const e2::Span screenX{0, screenW};
  const e2::Span screenY{0, screenH};

  // vertical sectioning of terminal
  const e2::Span mainY{screenY.first, screenY.last - g_cmd::widgetHeight};
  const e2::Span cmdY{mainY.last, mainY.last + g_cmd::widgetHeight};

  if (!e2::valid (screenY) || !e2::valid (screenX) || !e2::valid (mainY) ||
      !e2::valid (cmdY)) {
    return false;
  }

  {
    g_browsr::frame = e2::Box{screenX, mainY};
    const auto& browsrContentX = body (screenX);
    const auto& browsrContentY = body (mainY);
    // skip header, separator. leave 2 rows at end
    const auto dataY =
        e2::Span{first (browsrContentY) + 2, last (browsrContentY) - 1};
    g_browsr::headerSep = {
        screenX, first (browsrContentY) + 1
    };  // overlapping frame in X, to set connectors
    g_browsr::ambientSep = {screenX, last (dataY)};
    g_browsr::ambientLine = {browsrContentX, last (browsrContentY)};
  }

  {
    g_cmd::frame = e2::Box{screenX, cmdY};

    auto y = first (cmdY) + 1;
    g_cmd::queryStatusLine = e2::HLine{body (screenX), y++};
    g_cmd::statusSep = e2::HLine{
        screenX,  // include frame, to draw pipe connectors at line ends
        y++
    };

    // cmd input
    g_cmd::inputCaret = e2::GlobalCell{first (screenX) + 1, y};
    g_cmd::inputLine = e2::HLine{
        // skip border, leave space for caret ':'
        construct_relative (screenX, 2, size (screenX) - 1), y++
    };

    g_cmd::sepLine = e2::HLine{
        screenX,  // include frame, to draw pipe connectors at line ends
        y++
    };

    g_cmd::msgLine = e2::HLine{body (screenX), y};
  }

  // dynamically sized to content and
  // current screen size.
  APB_ASSERT (!g_overlay::content.empty());
  if (!size_and_set_overlay_widget (g_overlay::content)) {
    return false;
  };
  return true;
}
// --- end sizing --- //

// --- draw browser pane --- //

namespace draw_table {

static void header (
    const e2::HLine& headerLine, const std::span<const ColMetadata*> cols
)
{
  APB_ASSERT (valid (headerLine), headerLine);

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
    const auto fieldNameLen = static_cast<int> (fieldName.size());
    if (fieldNameLen < fieldWidth) {
      padL = static_cast<uint16_t> ((fieldWidth - fieldNameLen) / 2);
      padR = static_cast<uint16_t> (fieldWidth - fieldNameLen) - padL;
    }
    lpb_assembled = std::string (padL, ' ') + std::string (fieldName) +
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
        e2::VLine{.x = writeHead.x, .yspan = {writeHead.y, paneYLimit}},
        boxch::vertLine
    );
    writeHead.x++;
  }
}

struct Row1FrameArgs {
  int writeXStart;
  int writeXLimit;
  const std::span<const ColMetadata*> cols;

  bool valid() const noexcept
  {
    // check before calling row1
    return writeXStart <= writeXLimit;
  }
};

static void row1 (int writeY, sqlite3_stmt* br_dbRow, Row1FrameArgs fa)
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
    e2::write_string ({writeX, writeY}, fa.writeXLimit, cellText);
    writeX += static_cast<int> (fieldWidth);
    // jump the field separator
    writeX++;
    if (writeX > fa.writeXLimit) {
      break;
    }
  }
}

}  // namespace draw_table

template <>
struct fmt::formatter<draw_table::Row1FrameArgs>
    : fmt::formatter<std::string> {
  auto format (
      const draw_table::Row1FrameArgs& fa, format_context& ctx
  ) const
  {
    return fmt::formatter<std::string>::format (
        fmt::format (
            "Row1FrameArgs{{writeXStart: {}, writeXLimit: {}}}",
            fa.writeXStart, fa.writeXLimit
        ),
        ctx
    );
  }
};

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
          sqlite3_column_int64 (row, schema::UserReadViewSelect::start),
      .seq =
          [&row]() {
            const auto* p =
                sqlite3_column_text (row, schema::UserReadViewSelect::seq);
            const auto len = sqlite3_column_bytes (
                row, schema::UserReadViewSelect::seq
            );
            return std::string (
                reinterpret_cast<const char*> (p),
                static_cast<size_t> (len)
            );
          }(),
      .qual =
          [&row]() {
            const auto* br_p = sqlite3_column_text (
                row, schema::UserReadViewSelect::qual
            );
            const auto len = sqlite3_column_bytes (
                row, schema::UserReadViewSelect::qual
            );
            return std::string (
                reinterpret_cast<const char*> (br_p),
                static_cast<size_t> (len)
            );
          }(),
      .cig_br =
          [&row]() {
            return static_cast<const uint32_t*> (sqlite3_column_blob (
                row, schema::UserReadViewSelect::cig_uint32
            ));
          }(),
      .nCig =
          [&row]() {
            const auto rawNCig =
                sqlite3_column_int (row, schema::UserReadViewSelect::ncig);
            const auto cigBytes = sqlite3_column_bytes (
                row, schema::UserReadViewSelect::cig_uint32
            );
            APB_ASSERT (
                rawNCig >= 0 &&
                    static_cast<uint64_t> (rawNCig) * sizeof (uint32_t) <=
                        static_cast<uint64_t> (cigBytes),
                rawNCig, cigBytes
            );
            return static_cast<uint64_t> (rawNCig);
          }()
  };
};

}  // namespace

struct Seq1FrameArgs {
  const int16_t writeStartX;
  const int64_t writeStartXGPos;
  const e2::GlobalCell writeLimits;

  bool valid() const noexcept
  {
    return writeStartX >= 0 && writeStartXGPos >= 0 &&
           e2::valid (writeLimits);
  }
};

static e2::Delta seq1 (
    const int16_t yStart, sqlite3_stmt* br_dbRow, const Seq1FrameArgs& fa
)
{
  /* draw aligned data to aln pane */

  const auto& ref = g_db::locusInfo.refSlice;
  const auto pileupSpanGStart = g_db::locusInfo.start;
  const bool drawInsTrack = g_ui::drawTrackSwitches.ins;
  const bool drawQualTrack = g_ui::drawTrackSwitches.qual;

  constexpr uint8_t consumesNeitherQueryOrRef = 0b00;
  constexpr uint8_t consumesQueryOnly = 0b01;
  constexpr uint8_t consumesRefOnly = 0b10;
  constexpr uint8_t consumesQueryAndRef = 0b11;

  // preconditions
  if (fa.writeStartX >= fa.writeLimits.x || yStart >= fa.writeLimits.y) {
    return {0, 0};  // no-op
  }
  APB_ASSERT ((ref) ? !ref.value().empty() : true);
  APB_ASSERT (fa.writeStartXGPos >= pileupSpanGStart, fa.writeStartXGPos);

  e2::GlobalCell writeHead{{.x = fa.writeStartX, .y = yStart}};

  // ordered offsets for optional tracks
  constexpr int trackYOffsetBase = 1;
  const int trackYOffsetIns = trackYOffsetBase;  // first
  const int trackYOffsetQual =
      trackYOffsetBase + static_cast<int> (drawInsTrack);
  const int trackYOffsetInsQual = trackYOffsetBase +
                                  static_cast<int> (drawInsTrack) +
                                  static_cast<int> (drawQualTrack);

  bool enableInsTrack =
      drawInsTrack && (writeHead.y + trackYOffsetIns) < fa.writeLimits.y;
  bool enableQualTrack =
      drawQualTrack && (writeHead.y + trackYOffsetQual) < fa.writeLimits.y;

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
  size_t iRef =
      ref ? static_cast<size_t> (readFields.rStart - pileupSpanGStart) : 0;
  // track any insertion displayed on screen
  bool readInsDrawn = false;
  // buffer for subsequently writing quality string
  std::string qualDisplayBuf (
      static_cast<size_t> (fa.writeLimits.x - fa.writeStartX), ' '
  );
  // FOR EACH OP
  for (size_t iOp = 0; iOp < readFields.nCig; iOp++) {
    const auto op = readFields.cig_br[iOp];
    const auto opConsumeType = bam_cigar_type (op);
    const auto opType = bam_cigar_op (op);
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
        case (consumesNeitherQueryOrRef):
          continue;
        default:
          APB_UNREACHABLE ("CIGAR operation consumption type invalid");
      }
    }

    // At least partially on screen, to be processed
    const auto skipOffscreenBases =
        (iGc < fa.writeStartXGPos)
            ? static_cast<size_t> (fa.writeStartXGPos - iGc)
            : 0;

    if (opConsumeType == consumesQueryOnly) {
      if (opType == BAM_CINS && iGc > fa.writeStartXGPos) {
        // insertion
        // where at least one base PRIOR
        // to the insertion is visible

        // modify anchor base
        const auto anchorCell = writeHead - e2::dX (1);
        e2::clear_attrs (anchorCell);
        e2::extend (anchorCell, indicators::insRingAbove);

        if (enableInsTrack) {
          auto opInsWriteHead = anchorCell + e2::dY (trackYOffsetIns);
          if (opInsWriteHead.x < fa.writeLimits.x) {
            e2::set (opInsWriteHead, indicators::insAt);
            opInsWriteHead.x++;
          }
          for (size_t i = 0;
               i < opSz && opInsWriteHead.x < fa.writeLimits.x;
               ++i, ++opInsWriteHead.x) {
            set (
                opInsWriteHead,
                static_cast<uint32_t> (readFields.seq[iQuery + i])
            );
          }
          readInsDrawn = true;
        }
        else {
          // extra highlight if bases not unfolded
          e2::set_attr (anchorCell, {.fg = TB_UNDERLINE});
        }

        if (enableQualTrack) {
          auto opInsQualWriteHead =
              anchorCell + e2::dY (trackYOffsetInsQual);
          if (opInsQualWriteHead.x < fa.writeLimits.x) {
            e2::set (opInsQualWriteHead, indicators::insAt);
            opInsQualWriteHead.x++;
          }
          for (size_t i = 0;
               i < opSz && opInsQualWriteHead.x < fa.writeLimits.x;
               ++i, ++opInsQualWriteHead.x) {
            set (
                opInsQualWriteHead,
                static_cast<uint32_t> (readFields.qual[iQuery + i]),
                {.fg = TB_DIM}
            );
          }
        }
      }

      if (opType == BAM_CSOFT_CLIP && iOp == 0 && startToLEdge > 0) {
        // soft clipping at start of read
        // where cells available for writing
        // clipping label
        std::string clipLabel = "s(" + std::to_string (opSz) + ")";
        const int labSz = static_cast<int> (clipLabel.size());
        if (startToLEdge < labSz) {
          clipLabel = clipLabel.substr (
              static_cast<size_t> (labSz - startToLEdge)
          );
        }
        const auto drawnSz = static_cast<int> (clipLabel.size());
        e2::write_string (
            writeHead - e2::dX (drawnSz), fa.writeLimits.x, clipLabel,
            {.fg = TB_DIM}
        );
      }

      if (opType == BAM_CSOFT_CLIP && iOp == (readFields.nCig - 1)) {
        // soft clipping at end of read
        std::string clipLabel = "s(" + std::to_string (opSz) + ")";
        // if no space left, no-op
        e2::write_string (
            writeHead, fa.writeLimits.x, clipLabel, {.fg = TB_DIM}
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
        set (writeHead, 'x', {.fg = TB_UNDERLINE});
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
      for (size_t i = 0; i < opLenRemain && writeHead.x < fa.writeLimits.x;
           ++i, ++writeHead.x) {
        e2::Style dispStyle;
        auto dispChar = readFields.seq[iQuery + i];
        // mask bases that match the reference with '='.
        if (ref && (std::toupper (static_cast<unsigned char> (dispChar)) ==
                    std::toupper (
                        static_cast<unsigned char> ((*ref)[iRef + i])
                    ))) {
          dispChar = '=';
          dispStyle = {.fg = TB_DIM};
        }
        set (writeHead, static_cast<uint32_t> (dispChar), dispStyle);
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
    else if (opConsumeType == consumesNeitherQueryOrRef) {
      // as soft clipping (TODO factor out)
      if (opType == BAM_CHARD_CLIP && iOp == 0 && startToLEdge > 0) {
        std::string clipLabel = "h(" + std::to_string (opSz) + ")";
        const int labSz = static_cast<int> (clipLabel.size());
        if (startToLEdge < labSz) {
          clipLabel = clipLabel.substr (
              static_cast<size_t> (labSz - startToLEdge)
          );
        }
        const auto drawnSz = static_cast<int> (clipLabel.size());
        e2::write_string (
            writeHead - e2::dX (drawnSz), fa.writeLimits.x, clipLabel,
            {.fg = TB_DIM}
        );
      }

      if (opType == BAM_CHARD_CLIP && iOp == (readFields.nCig - 1)) {
        std::string clipLabel = "h(" + std::to_string (opSz) + ")";
        // if no space left, no-op
        e2::write_string (
            writeHead, fa.writeLimits.x, clipLabel, {.fg = TB_DIM}
        );
      }
    }
    else {
      APB_UNREACHABLE ("CIGAR operation consumption type invalid");
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
        fa.writeLimits.x, qualDisplayBuf, {.fg = TB_DIM}
    );
    writeHead.y++;
  }
  if (enableQualTrack && readInsDrawn) {
    writeHead.y++;
  }

  return {.dx = writeHead.x - fa.writeStartX, .dy = writeHead.y - yStart};
}

}  // namespace draw_aln

template <>
struct fmt::formatter<draw_aln::Seq1FrameArgs>
    : fmt::formatter<std::string> {
  auto format (
      const draw_aln::Seq1FrameArgs& fa, format_context& ctx
  ) const
  {
    return fmt::formatter<std::string>::format (
        fmt::format (
            "Seq1FrameArgs{{writeStartX: {}, writeStartXGPos: {}, "
            "writeLimits: ({}, {})}}",
            fa.writeStartX, fa.writeStartXGPos, fa.writeLimits.x,
            fa.writeLimits.y
        ),
        ctx
    );
  }
};

namespace draw_query_data {

static WidgetStatus draw_query_data()
{
  // draw reads and data table
  if (!valid (g_browsr::frame) || size (g_browsr::frame.xspan) < 4 ||
      size (g_browsr::frame.yspan) < 8) {
    // bounds slightly approximate
    return {WidgetStatus::insufficientSz};
  }
  APB_ASSERT (g_db::locusInfo.valid(), g_db::locusInfo);

  sqlite3_reset (g_db::selectStmt);

  /* size widgets */
  uint16_t tableWidth = 0;
  std::vector<const ColMetadata*> activeCols;
  for (const auto& col : g_ui::tableCols) {
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
      std::max (0, size (g_browsr::frame.xspan) - 2 - minAlnPaneWidth)
  );
  tableWidth = std::min (tableWidth, maxTableWidth);
  // No visible columns leaves nothing to lay the table pane out
  // with; fall back to the no-table-pane layout below.
  const bool showTable = g_ui::drawPaneSwitches.table && tableWidth > 0;

  const auto& [frameX, frameY] = spans (g_browsr::frame);
  const auto& contentX = body (frameX);
  const auto& contentY = body (frameY);
  // skip header, separator. leave 2 rows at end
  const auto dataY = e2::Span{first (contentY) + 2, last (contentY) - 1};

  if (showTable) {
    const int splitAbsX = last (contentX) - tableWidth;
    e2::Span alnPaneX{first (contentX), splitAbsX};
    e2::Span tablePaneX{splitAbsX + 1, last (contentX)};

    g_browsr::alnPaneRefLine = {alnPaneX, first (contentY)};
    g_browsr::tablePaneHeaderLine = {tablePaneX, first (contentY)};
    g_browsr::alnPaneDataBox = {alnPaneX, dataY};
    g_browsr::tablePaneDataBox = {tablePaneX, dataY};
    g_browsr::vSep = {
        splitAbsX, construct_relative (frameY, 0, size (frameY) - 1)
    };

    APB_ASSERT (
        valid (g_browsr::tablePaneHeaderLine),
        g_browsr::tablePaneHeaderLine
    );
    APB_ASSERT (
        size (g_browsr::tablePaneHeaderLine) > 0,
        size (g_browsr::tablePaneHeaderLine)
    );
    APB_ASSERT (
        valid (g_browsr::tablePaneDataBox), g_browsr::tablePaneDataBox
    );
    APB_ASSERT (
        height (g_browsr::tablePaneDataBox) > 0,
        height (g_browsr::tablePaneDataBox)
    );
    APB_ASSERT (
        width (g_browsr::tablePaneDataBox) > 0,
        width (g_browsr::tablePaneDataBox)
    );
  }
  else {
    g_browsr::tablePaneHeaderLine = {};  // invalid
    g_browsr::tablePaneDataBox = {};
    g_browsr::vSep = {};

    g_browsr::alnPaneRefLine = {contentX, first (contentY)};
    g_browsr::alnPaneDataBox = {contentX, dataY};
  }
  APB_ASSERT (valid (g_browsr::alnPaneDataBox), g_browsr::alnPaneDataBox);
  APB_ASSERT (
      height (g_browsr::alnPaneDataBox) > 0,
      height (g_browsr::alnPaneDataBox)
  );
  APB_ASSERT (
      width (g_browsr::alnPaneDataBox) > 0,
      width (g_browsr::alnPaneDataBox)
  );
  /* end size widgets */

  /* configure/validate draw coordinates */
  const auto alnPaneHalfWidth = width (g_browsr::alnPaneDataBox) / 2;
  // Offset needed for the pane's left edge to reach
  // db.locusInfo.start. Can be positive when there isn't room
  // to center on pos, e.g. a locus near the start of its contig, or reads
  // that don't extend a full half-pane-width left of pos.
  const auto marginToPileupStart =
      g_db::locusInfo.start - g_db::locusInfo.pos + alnPaneHalfWidth;
  const auto marginToPileupEnd = std::max (
      g_db::locusInfo.end - alnPaneHalfWidth - g_db::locusInfo.pos,
      marginToPileupStart  // keep clamp's [lo, hi] non-empty
  );
  g_browsr::userPanOffset = std::clamp (
      g_browsr::userPanOffset, marginToPileupStart, marginToPileupEnd
  );
  const auto alnPaneLeftmostGPos =
      g_db::locusInfo.pos - alnPaneHalfWidth + g_browsr::userPanOffset;
  APB_ASSERT (alnPaneLeftmostGPos >= 0, alnPaneLeftmostGPos);
  /* end coordinates */

  if (g_db::locusInfo.refSlice) {
    /* draw reference */
    const int64_t offsetToLocusStart =
        g_db::locusInfo.start - alnPaneLeftmostGPos;

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
        {first (g_browsr::alnPaneRefLine.xspan) +
             static_cast<int> (startDrawX),
         g_browsr::alnPaneRefLine.y},
        last (g_browsr::alnPaneRefLine.xspan),
        g_db::locusInfo.refSlice->substr (
            static_cast<size_t> (skipRefBases)
        )
    );
  }

  if (showTable) {
    draw_table::header (g_browsr::tablePaneHeaderLine, activeCols);
    draw_table::row_separators (g_browsr::tablePaneDataBox, activeCols);
    /* draw pane separator */
    set (body (g_browsr::vSep), boxch::vertLine, {.fg = TB_DIM});
    set (first (g_browsr::vSep), boxch::downTConnect, {.fg = TB_DIM});
    set (last (g_browsr::vSep), boxch::upTConnect, {.fg = TB_DIM});
  }

  /* iteratively draw query data */
  auto seqWriteHead = vertexA (g_browsr::alnPaneDataBox);
  auto seqWriteLim = vertexC (g_browsr::alnPaneDataBox) +
                     e2::dXY (1, 1);  // exclusive limit
  const draw_aln::Seq1FrameArgs seq1Frame{
      .writeStartX = static_cast<int16_t> (seqWriteHead.x),
      .writeStartXGPos = alnPaneLeftmostGPos,
      .writeLimits = seqWriteLim,
  };
  APB_ASSERT (seq1Frame.valid(), seq1Frame);
  const draw_table::Row1FrameArgs row1Frame{
      .writeXStart = first (g_browsr::tablePaneDataBox.xspan),
      .writeXLimit = last (g_browsr::tablePaneDataBox.xspan),
      .cols = activeCols,
  };
  APB_ASSERT (row1Frame.valid(), row1Frame);
  // FIXME: display path of current reads
  // FIXME: Always sort by path!
  // const char* curPath = nullptr;
  if (g_db::nStmtRows > 0) {
    uint16_t nReadDrawn = 0;
    for (uint16_t iRead = 0; seqWriteHead.y < seqWriteLim.y; ++iRead) {
      const auto iterStatus = query::next_read (g_db::selectStmt);
      if (!iterStatus) {
        // selectStmt is only ever installed after a full count_rows
        // pass already succeeded against this exact data (see
        // main()'s startup query / try_apply_query_clause), and
        // nothing writes to db afterwards.
        APB_UNREACHABLE (
            fmt::format (
                "failed to step query during render: {}",
                sqlite3_errmsg (g_db::conn)
            )
        );
      }
      if (*iterStatus == query::RowIterStatus::rowAvail) {
        if (static_cast<int64_t> (iRead) < g_db::stmtRowScrollOffset) {
          // reads hidden by scrolling
          continue;
        }
        const auto dHead = draw_aln::seq1 (
            static_cast<int16_t> (seqWriteHead.y), g_db::selectStmt,
            seq1Frame
        );
        if (showTable) {
          draw_table::row1 (seqWriteHead.y, g_db::selectStmt, row1Frame);
        }
        seqWriteHead.y += dHead.dy;
        ++nReadDrawn;
      }
      else {
        break;
      }
    }
    g_browsr::nReadOnscreen = nReadDrawn;

    /* draw crosshair */
    // Bounds-check before narrowing: userPanOffset can be far
    // larger than an int16_t can hold, so compare in the wide
    // type first and only truncate once known on-screen.
    if (const int64_t pileupScreenXPosWide =
            first (g_browsr::alnPaneDataBox.xspan) + alnPaneHalfWidth -
            g_browsr::userPanOffset;
        pileupScreenXPosWide >= first (g_browsr::alnPaneDataBox.xspan) &&
        pileupScreenXPosWide < last (g_browsr::alnPaneDataBox.xspan)) {
      // if user has not scrolled crosshair offscreen:
      const auto pileupScreenXPos =
          static_cast<int16_t> (pileupScreenXPosWide);
      e2::VLine pileupCrosshair{
          pileupScreenXPos, g_browsr::alnPaneDataBox.yspan
      };
      add_attr (pileupCrosshair, {.fg = TB_REVERSE});
      // draw marker linking reference base and query position of pileup
      set (
          e2::GlobalCell{
              pileupScreenXPos, first (g_browsr::alnPaneDataBox.yspan) - 1
          },
          '|', {.fg = TB_DIM}
      );
    }
    /* end draw crosshair */
  }
  else {
    e2::write_string (
        seqWriteHead, seqWriteLim.x, "no reads at locus for current query",
        {.fg = TB_DIM}
    );
  }
  /* end draw query data */

  return {WidgetStatus::success};
}

}  // namespace draw_query_data


// --- end draw browser pane --- //

WidgetStatus draw_main_ui()
{
  // NOTE: set order does matter,
  // since some places just overwrite
  // previous draw calls
  APB_ASSERT (validate::ui_is_valid());

  {
    // draw browser chrome
    // preconds
    // FIXME: these should not be asserts!
    APB_ASSERT (width (g_browsr::frame) > 1, width (g_browsr::frame));
    APB_ASSERT (height (g_browsr::frame) > 1, height (g_browsr::frame));

    const auto& bFrame = g_browsr::frame;
    set (vertexA (bFrame), boxch::topLeftRoundCorner, {.fg = TB_DIM});
    set (vertexB (bFrame), boxch::topRightRoundCorner, {.fg = TB_DIM});

    set (body (edgeAB (bFrame)), boxch::horzLine, {.fg = TB_DIM});
    set (
        construct_relative (edgeDA (bFrame), 1, height (bFrame)),
        boxch::vertLine, {.fg = TB_DIM}
    );
    set (
        construct_relative (edgeBC (bFrame), 1, height (bFrame)),
        boxch::vertLine, {.fg = TB_DIM}
    );

    set (body (g_browsr::headerSep), boxch::horzLine, {.fg = TB_DIM});
    set (
        first (g_browsr::headerSep), boxch::rightTConnect, {.fg = TB_DIM}
    );

    set (body (g_browsr::ambientSep), boxch::horzLine, {.fg = TB_DIM});
    set (
        first (g_browsr::ambientSep), boxch::rightTConnect, {.fg = TB_DIM}
    );
    set (last (g_browsr::ambientSep), boxch::leftTConnect, {.fg = TB_DIM});

    set (last (g_browsr::headerSep), boxch::leftTConnect, {.fg = TB_DIM});
  }
  {
    // draw cmd chrome
    // preconds
    APB_ASSERT (width (g_cmd::frame) > 1, width (g_cmd::frame));
    APB_ASSERT (height (g_cmd::frame) > 1, height (g_cmd::frame));

    const auto& cFrame = g_cmd::frame;
    set (vertexA (cFrame), boxch::topLeftRoundCorner, {.fg = TB_DIM});
    set (vertexB (cFrame), boxch::topRightRoundCorner, {.fg = TB_DIM});
    set (vertexD (cFrame), boxch::bottomLeftRoundCorner, {.fg = TB_DIM});
    set (vertexC (cFrame), boxch::bottomRightRoundCorner, {.fg = TB_DIM});

    set (body (edgeAB (cFrame)), boxch::horzHeavy, {.fg = TB_DIM});
    set (body (edgeDA (cFrame)), boxch::vertLine, {.fg = TB_DIM});
    set (body (edgeBC (cFrame)), boxch::vertLine, {.fg = TB_DIM});
    set (body (g_cmd::statusSep), boxch::horzLine, {.fg = TB_DIM});

    set (g_cmd::inputCaret, ':');
    set (body (g_cmd::sepLine), boxch::horzLine, {.fg = TB_DIM});
  }

  // TODO here is the only point where insufficient size is used
  // vaguely properly. Currently program exits rather than crashing
  // on tiny sizes only because this reports insufficient size.
  switch (const auto dqStatus = draw_query_data::draw_query_data();
          dqStatus.code) {
    case WidgetStatus::success:
      break;
    case WidgetStatus::insufficientSz:
      return dqStatus;
  }

  {
    // draw pileup ambient
    const auto& locusData = g_db::locusInfo;
    // preconds
    APB_ASSERT (locusData.valid(), locusData);

    auto writeHead = first (g_browsr::ambientLine);
    const auto lineEnd = last (g_browsr::ambientLine.xspan);
    writeHead.x++;  // initial space
    writeHead.x +=
        e2::write_string (writeHead, lineEnd, "LOCUS:", {.fg = TB_DIM});
    writeHead.x++;  // space
    writeHead.x += e2::write_string (
        writeHead, lineEnd,
        fmt::format ("{}:{}", locusData.contig, locusData.pos)
    );
    writeHead.x++;  // space
    set (writeHead, boxch::vertLine, {.fg = TB_DIM});
    writeHead.x += 2;  // past bar, then space
    writeHead.x +=
        e2::write_string (writeHead, lineEnd, "SPAN:", {.fg = TB_DIM});
    writeHead.x++;  // space
    writeHead.x += e2::write_string (
        writeHead, lineEnd,
        fmt::format ("{}-{}", locusData.start, locusData.end)
    );
    writeHead.x++;  // space
    set (writeHead, boxch::vertLine, {.fg = TB_DIM});
  }
  {
    // draw cmd
    const auto& userQuery = g_db::userClause;

    e2::write_string (
        first (g_cmd::inputLine), last (g_cmd::inputLine).x,
        g_cmd::inputBuf.text
    );
    auto cursorCell = first (g_cmd::inputLine) +
                      e2::dX (static_cast<int> (g_cmd::inputBuf.curs));
    if (cursorCell.x < last (g_cmd::inputLine).x) {
      e2::add_attr (cursorCell, {.fg = TB_REVERSE});
    }
    e2::write_string (
        first (g_cmd::msgLine), last (g_cmd::msgLine).x, g_cmd::msgBuf,
        {.fg = TB_DIM}
    );

    // stringify query
    std::string userClauseString;
    if (!userQuery.where.empty()) {
      userClauseString.append ("WHERE ");
      userClauseString.append (
          query::build_where_clause (userQuery.where)
      );
    }

    if (!userQuery.orderBy.empty()) {
      userClauseString.append (" ORDER BY ");
      userClauseString.append (userQuery.orderBy);
    }

    e2::write_string (
        first (g_cmd::queryStatusLine), last (g_cmd::queryStatusLine).x,
        userClauseString, {.fg = TB_DIM}
    );
  }

  // should remain true (postcondition)
  APB_ASSERT (validate::ui_is_valid());
  return {};
}

void draw_overlay()
{
  APB_ASSERT (validate::overlay_widget_is_valid());
  APB_ASSERT (!g_overlay::content.empty());

  const auto& box = g_overlay::contentBox;
  const auto& frame = g_overlay::frame;
  const auto& content = g_overlay::content;

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

  auto xEnd = last (box.xspan) - 1;  // leave a gap before the border

  auto writeHead = vertexA (frame);
  writeHead.x += 1;
  writeHead.x += e2::write_string (
      writeHead, xEnd, " q: close overlay ", {.fg = TB_DIM}
  );
  writeHead.x += 3;
  const auto lnN = height (box);
  if (lnN < std::ssize (content)) {
    e2::write_string (
        writeHead, xEnd, "Up / Down: scroll", {.fg = TB_DIM}
    );
  }

  const auto maxLnOff =
      std::max (0, static_cast<int> (std::ssize (content)) - lnN);
  const auto lnOff = static_cast<size_t> (
      std::clamp (g_overlay::contentLnOffset, 0, maxLnOff)
  );
  auto lnY = extb::vertexA (box);
  for (int i = 0; i < lnN && i < std::ssize (content); ++i) {
    e2::write_string (lnY, xEnd, content[static_cast<size_t> (i) + lnOff]);
    ++lnY.y;
  }
}
