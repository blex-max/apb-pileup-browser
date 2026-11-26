#include "widgets.hpp"

#include <fmt/format.h>
#include <htslib/sam.h>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "apb_db/apb_db_schema.hpp"
#include "drawing_chars.hpp"
#include "extb/box/box.hpp"
#include "extb/box/span.hpp"
#include "extb/extb.hpp"
#include "helpblocks.hpp"
#include "query.hpp"
#include "sqlite_col.hpp"
#include "tui/state.hpp"
#include "tui_assert.hpp"

namespace e2 = extb;

namespace g_db = tui_state::db;
namespace g_viewer = tui_state::ui::viewer;
namespace g_cmd = tui_state::ui::cmd;
namespace g_overlay = tui_state::ui::overlay;
namespace g_ui = tui_state::ui;

constexpr e2::Style none{};
constexpr e2::Style dim{.fg = TB_DIM};
constexpr e2::Style bold{.fg = TB_BOLD};

// integer column as text, NULL as *
static std::string val_or_star (sqlite3_stmt* row, int idx)
{
  const auto v = col_to_string (row, idx);
  return v ? *v : "*";
}

namespace tui_state::ui {

// FIXME: add tests for NULL -> "*"
constinit std::array<ColMetadata, 10> tableCols{{
    {false, "qpos", 6,
     [] (sqlite3_stmt* row) {
       return val_or_star (row, schema::UserReadViewSelect::qpos);
     }},
    {true, "basequal", 12,
     [] (sqlite3_stmt* row) {
       return val_or_star (row, schema::UserReadViewSelect::basequal);
     }},
    {true, "flag", 6,
     [] (sqlite3_stmt* row) {
       return val_or_star (row, schema::UserReadViewSelect::flag);
     }},
    {true, "mapq", 6,
     [] (sqlite3_stmt* row) {
       return val_or_star (row, schema::UserReadViewSelect::mapq);
     }},
    {false, "start", 14,
     [] (sqlite3_stmt* row) {
       return std::to_string (
           sqlite3_column_int64 (row, schema::UserReadViewSelect::start)
       );
     }},
    {false, "end", 14,
     [] (sqlite3_stmt* row) {
       return val_or_star (row, schema::UserReadViewSelect::end);
     }},
    {true, "cigar", 16,
     [] (sqlite3_stmt* row) {
       return val_or_star (row, schema::UserReadViewSelect::cigar);
     }},
    {false, "qname", 21,
     [] (sqlite3_stmt* row) {
       return val_or_star (row, schema::UserReadViewSelect::qname);
     }},
    {false, "mtid", 10,
     [] (sqlite3_stmt* row) {
       return val_or_star (row, schema::UserReadViewSelect::mtid);
     }},
    {false, "mstart", 14, [] (sqlite3_stmt* row) {
       return val_or_star (row, schema::UserReadViewSelect::mstart);
     }},
}};

}  // namespace tui_state::ui

// --- helpers --- //

namespace {

bool viewer_widget_is_valid()
{
  return valid (g_viewer::frame) &&
         size (g_viewer::frame.xspan) >= g_ui::kMinWidth &&
         size (g_viewer::frame.yspan) >= g_viewer::kMinWidgetHeight;
};
bool cmd_widget_is_valid()
{
  return valid (g_cmd::frame) &&
         size (g_cmd::frame.xspan) >= g_ui::kMinWidth &&
         size (g_cmd::frame.yspan) == g_cmd::kWidgetHeight;
};

#define APB_ASSERT_MAIN_UI_VALID           \
  do {                                     \
    TUI_ASSERT (tb_width() > 0);           \
    TUI_ASSERT (tb_height() > 0);          \
    TUI_ASSERT (viewer_widget_is_valid()); \
    TUI_ASSERT (cmd_widget_is_valid());    \
  } while (false)

#define APB_ASSERT_BOX_NONEMPTY(box)             \
  do {                                           \
    TUI_ASSERT (valid (box), box);               \
    TUI_ASSERT (height (box) > 0, height (box)); \
    TUI_ASSERT (width (box) > 0, width (box));   \
  } while (false)

#define APB_ASSERT_OVERLAY_UI_VALID                                   \
  do {                                                                \
    TUI_ASSERT (valid (g_overlay::frame), g_overlay::frame);          \
    TUI_ASSERT (                                                      \
        size (g_overlay::frame.xspan) >= g_overlay::kMinWidgetWidth,  \
        g_overlay::frame                                              \
    );                                                                \
    TUI_ASSERT (                                                      \
        size (g_overlay::frame.yspan) >= g_overlay::kMinWidgetHeight, \
        g_overlay::frame                                              \
    );                                                                \
  } while (false)

// file heading: path, then read counts if the file has reads
// at the locus, e.g. a.bam: 12/300 reads. The path hides
// leading elements to fit, e.g. …/samples/a.bam; if the name
// alone is too long, it is truncated, e.g. …/a.b…
void draw_source_name (e2::GlobalCell start, int xlim, const AlnFile& file)
{
  constexpr e2::Style style{.fg = TB_REVERSE};
  const auto counts =
      file.nTotal > 0
          ? fmt::format (": {}/{} reads", file.nQuery, file.nTotal)
          : std::string{};
  // path up to the counts
  const int pathLim = xlim - static_cast<int> (counts.size());
  if (pathLim > start.x) {
    std::string_view path = file.path;
    if (path.size() > static_cast<size_t> (pathLim - start.x)) {
      // first separator leaving room for the ellipsis
      auto iSep = path.find (
          '/', path.size() + 1 - static_cast<size_t> (pathLim - start.x)
      );
      if (iSep == std::string_view::npos) {
        iSep = path.rfind ('/');  // name only
      }
      if (iSep != std::string_view::npos && iSep > 0) {
        e2::set (start, ellipsis, style);
        ++start.x;
        path.remove_prefix (iSep);
      }
    }
    if (path.size() > static_cast<size_t> (pathLim - start.x)) {
      // name alone too long: ellipsis in last cell
      e2::set (
          e2::GlobalCell{{.x = pathLim - 1, .y = start.y}}, ellipsis, style
      );
      e2::write_string (start, pathLim - 1, path, style);
      start.x = pathLim;
    }
    else {
      start.x += e2::write_string (start, pathLim, path, style);
    }
  }
  e2::write_string (start, xlim, counts, style);
}

// colour for a base if drawColour, e.g. A green; else default
uintattr_t base_colour (uint32_t base)
{
  if (!g_ui::drawColour) {
    return TB_DEFAULT;
  }
  switch (base) {
  case 'A':
  case 'a':
    return TB_GREEN;
  case 'C':
  case 'c':
    return TB_BLUE;
  case 'G':
  case 'g':
    return TB_YELLOW;  // orange, conventionally
  case 'T':
  case 't':
    return TB_RED;
  default:
    return TB_DEFAULT;
  }
}

}  // namespace

// --- end helpers --- //

// --- size calculation --- //

bool size_and_set_overlay_widget (helpblocks::TextBlockRef content)
{
  /* set overlay widget, dynamically sizing to content */
  TUI_ASSERT (!content.empty());

  const auto screenW = tb_width();
  const auto screenH = tb_height();

  const auto availW =
      static_cast<int> (std::ceil (static_cast<double> (screenW) * 0.6));
  if (availW < g_overlay::kMinWidgetWidth) {
    // too small
    return false;
  }
  const auto availH =
      static_cast<int> (std::ceil (static_cast<double> (screenH) * 0.6));
  if (availH < g_overlay::kMinWidgetHeight) {
    // too small
    return false;
  }

  // dynamically sized to content
  const auto framedContentH = static_cast<int> (content.size() + 2);
  const auto wgtH = std::min<int> (framedContentH, availH);

  const auto maxLineW =
      std::ranges::max_element (content, {}, &std::string_view::size)
          ->size();
  // +2 for the left/right border, +1 for a gap before the
  // right border
  const auto framedContentW = static_cast<int> (maxLineW + 3);
  const auto wgtW = std::min (framedContentW, availW);

  const auto xOff = static_cast<int> (std::floor ((screenW - wgtW) / 2));
  const auto yOff = static_cast<int> (std::floor ((screenH - wgtH) / 2));

  e2::Span ySpan{yOff, yOff + wgtH};
  e2::Span xSpan{xOff, xOff + wgtW};

  g_overlay::frame = e2::Box{xSpan, ySpan};
  g_overlay::contentBox = e2::Box{body (xSpan), body (ySpan)};
  g_overlay::content = content;

  // postconditions:
  APB_ASSERT_OVERLAY_UI_VALID;
  return true;
}

bool size_main_widgets()
{
  const auto screenW = tb_width();
  const auto screenH = tb_height();
  if (tb_width() < g_ui::kMinWidth ||
      tb_height() < g_cmd::kWidgetHeight + g_viewer::kMinWidgetHeight) {
    return false;
  }

  const e2::Span screenX{0, screenW};
  const e2::Span screenY{0, screenH};

  // vertical sectioning of terminal
  const e2::Span mainY{screenY.first, screenY.last - g_cmd::kWidgetHeight};
  const e2::Span cmdY{mainY.last, mainY.last + g_cmd::kWidgetHeight};

  if (!e2::valid (screenY) || !e2::valid (screenX) || !e2::valid (mainY) ||
      !e2::valid (cmdY)) {
    return false;
  }

  {
    g_viewer::frame = e2::Box{screenX, mainY};
    // skip header, separator.
    g_viewer::headerSep = {
        screenX, first (body (mainY)) + 1
    };  // overlapping frame in X, to set connectors
  }

  {
    g_cmd::frame = e2::Box{screenX, cmdY};

    auto y = first (cmdY);
    g_cmd::contextLine = e2::HLine{body (screenX), y++};
    // g_cmd::inputSep = e2::HLine{
    //     screenX,  // include frame, to draw pipe connectors at line ends
    //     y++
    // };

    // cmd input
    g_cmd::promptChar = e2::GlobalCell{first (screenX) + 1, y};
    g_cmd::inputLine = e2::HLine{
        // skip border, leave space for caret ':'
        construct_relative (screenX, 2, size (screenX) - 1), y++
    };

    g_cmd::msgLine = e2::HLine{body (screenX), y};
  }

  APB_ASSERT_MAIN_UI_VALID;
  return true;
}
// --- end sizing --- //

// --- draw read viewer --- //

namespace draw_table {

static void header (
    const e2::HLine& headerLine, const std::span<const ColMetadata*> cols
)
{
  TUI_ASSERT (valid (headerLine), headerLine);

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
    if (writeHead.x >= xLim) {
      break;
    }
    set (writeHead, boxLight.vert);
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
    if (writeHead.x >= writeXLimit) {
      break;
    }
    set (
        e2::VLine{.x = writeHead.x, .yspan = {writeHead.y, paneYLimit}},
        boxLight.vert
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

  std::string to_string() const
  {
    return fmt::format (
        "Row1FrameArgs{{writeXStart: {}, writeXLimit: {}}}", writeXStart,
        writeXLimit
    );
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

namespace draw_aln {

namespace {
// seq1 internals

struct ReadAlnFields {
  int64_t rStart;
  std::optional<std::string> seq;
  std::optional<std::string> qual;
  const uint32_t* cig_br;
  uint64_t nCig;
};


uint32_t seq_char (const std::optional<std::string>& seq, size_t i)
{
  return seq ? static_cast<unsigned char> ((*seq)[i]) : missing_ch;
}

uint32_t qual_char (
    const std::optional<std::string>& qual, size_t i, bool useSparkline
)
{
  if (qual) {
    if (useSparkline) {
      return qual_to_sparkline (static_cast<unsigned char> ((*qual)[i]));
    }
    return static_cast<unsigned char> ((*qual)[i]);
  }
  return missing_ch;
}

bool is_clip (uint32_t op)
{
  return bam_cigar_op (op) == BAM_CSOFT_CLIP ||
         bam_cigar_op (op) == BAM_CHARD_CLIP;
}

// e.g. s(4), h(4)
std::string clip_label (uint32_t op)
{
  return fmt::format (
      "{}({})", bam_cigar_op (op) == BAM_CSOFT_CLIP ? 's' : 'h',
      bam_cigar_oplen (op)
  );
}

ReadAlnFields get_seq1_read_fields (sqlite3_stmt* br_stmtRow)
{
  return {
      .rStart = sqlite3_column_int64 (
          br_stmtRow, schema::UserReadViewSelect::start
      ),
      .seq = col_text (br_stmtRow, schema::UserReadViewSelect::seq),
      .qual = col_text (br_stmtRow, schema::UserReadViewSelect::qual),
      .cig_br = static_cast<const uint32_t*> (sqlite3_column_blob (
          br_stmtRow, schema::UserReadViewSelect::cig32
      )),
      // after column_blob, per sqlite3 docs
      .nCig = static_cast<uint64_t> (sqlite3_column_bytes (
                  br_stmtRow, schema::UserReadViewSelect::cig32
              )) /
              sizeof (uint32_t)
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

  std::string to_string() const
  {
    return fmt::format (
        "Seq1FrameArgs{{writeStartX: {}, writeStartXGPos: {}, "
        "writeLimits: ({}, {})}}",
        writeStartX, writeStartXGPos, writeLimits.x, writeLimits.y
    );
  }
};

// Draw clips outward from the read's aligned span: soft
// clips as bracketed bases, e.g. (ACGT), if drawClips and
// seq present, else as labels, e.g. s(4); hard clips as
// labels, e.g. h(4). Truncated at pan bounds (see
// draw_main_to_buffer).
static void clips1 (
    const int16_t yStart, const ReadAlnFields& readFields,
    const Seq1FrameArgs& fa, const std::optional<int> qualYOffset,
    const e2::Style& qualStyle
)
{
  const auto* cig = readFields.cig_br;
  const auto nCig = readFields.nCig;

  // set cell by genomic position; no-op outside pane
  const auto setAtGPos = [&fa, yStart] (
                             int64_t gPos, int dy, uint32_t ch,
                             const e2::Style& style
                         ) {
    const int64_t x = fa.writeStartX + (gPos - fa.writeStartXGPos);
    if (x >= fa.writeStartX && x < fa.writeLimits.x) {
      e2::set (
          e2::GlobalCell{{.x = static_cast<int> (x), .y = yStart + dy}},
          ch, style
      );
    }
  };
  const auto asBases = [&readFields] (uint32_t op) {
    return g_ui::drawClips && bam_cigar_op (op) == BAM_CSOFT_CLIP &&
           readFields.seq;
  };
  const auto clipWidth = [&asBases] (uint32_t op) {
    return asBases (op) ? static_cast<int64_t> (bam_cigar_oplen (op)) + 2
                        : static_cast<int64_t> (clip_label (op).size());
  };
  const auto drawClip = [&] (int64_t gStart, size_t iOp) {
    const auto op = cig[iOp];
    if (!asBases (op)) {
      const auto label = clip_label (op);
      for (size_t i = 0; i < label.size(); ++i) {
        setAtGPos (
            gStart + static_cast<int64_t> (i), 0,
            static_cast<unsigned char> (label[i]), none
        );
      }
      return;
    }
    const auto len = bam_cigar_oplen (op);
    const auto qStart =
        static_cast<size_t> (bam_cigar2qlen (static_cast<int> (iOp), cig));
    setAtGPos (gStart, 0, '(', bold);
    for (size_t i = 0; i < len; ++i) {
      const auto gBase = gStart + 1 + static_cast<int64_t> (i);
      const auto base = seq_char (readFields.seq, qStart + i);
      setAtGPos (gBase, 0, base, {.fg = base_colour (base)});
      if (qualYOffset) {
        setAtGPos (
            gBase, *qualYOffset,
            qual_char (readFields.qual, qStart + i, g_ui::drawSparkline),
            qualStyle
        );
      }
    }
    setAtGPos (gStart + len + 1, 0, ')', bold);
  };

  // leading clips, leftward from read start
  size_t nLead = 0;
  while (nLead < nCig && is_clip (cig[nLead])) {
    ++nLead;
  }
  auto gPos = readFields.rStart;
  for (size_t iOp = nLead; iOp > 0; --iOp) {
    gPos -= clipWidth (cig[iOp - 1]);
    drawClip (gPos, iOp - 1);
  }

  // trailing clips, rightward from read end
  size_t iTrail = nCig;
  while (iTrail > nLead && is_clip (cig[iTrail - 1])) {
    --iTrail;
  }
  gPos = readFields.rStart + bam_cigar2rlen (static_cast<int> (nCig), cig);
  for (size_t iOp = iTrail; iOp < nCig; ++iOp) {
    drawClip (gPos, iOp);
    gPos += clipWidth (cig[iOp]);
  }
}

// bam_cigar_type bit masks
constexpr uint8_t consumesQuery = 0b01;
constexpr uint8_t consumesRef = 0b10;

// An insertion, anchored on the base before it
struct InsPlacement {
  int64_t anchorGPos;
  size_t iQuery;  // first inserted base
  uint32_t len;
  int row;  // of the insertion track
};

// Place each insertion whose anchor is on screen on the lowest
// insertion track row where its text, ^ + bases, overlaps no
// other. Returns the placements and the number of rows used.
static std::pair<std::vector<InsPlacement>, int> place_ins (
    const ReadAlnFields& readFields, const Seq1FrameArgs& fa
)
{
  const int64_t rightGPos =
      fa.writeStartXGPos + (fa.writeLimits.x - fa.writeStartX);
  std::vector<InsPlacement> placements;
  // per row, gpos just past the last insertion on it
  std::vector<int64_t> rowEnds;
  auto iGc = readFields.rStart;
  size_t iQuery = 0;
  for (size_t iOp = 0; iOp < readFields.nCig; iOp++) {
    const auto op = readFields.cig_br[iOp];
    const auto len = bam_cigar_oplen (op);
    const auto anchorGPos = iGc - 1;
    if (bam_cigar_op (op) == BAM_CINS &&
        anchorGPos >= fa.writeStartXGPos && anchorGPos < rightGPos) {
      size_t row = 0;
      while (row < rowEnds.size() && rowEnds[row] > anchorGPos) {
        ++row;
      }
      if (row == rowEnds.size()) {
        rowEnds.push_back (0);
      }
      rowEnds[row] = anchorGPos + 1 + len;
      placements.push_back (
          {anchorGPos, iQuery, len, static_cast<int> (row)}
      );
    }
    if (bam_cigar_type (bam_cigar_op (op)) & consumesQuery) {
      iQuery += len;
    }
    if (bam_cigar_type (bam_cigar_op (op)) & consumesRef) {
      iGc += len;
    }
  }
  return {placements, static_cast<int> (rowEnds.size())};
}

// Draw insertions placed by place_ins: a ring over each anchor
// base, and ^ + inserted bases on the insertion's row of the
// insertion track, with quality beneath on the same row of the
// insertion quality track. Tracks are drawn if offsets given.
static void ins1 (
    const int16_t yStart, const ReadAlnFields& readFields,
    const Seq1FrameArgs& fa, std::span<const InsPlacement> placements,
    const std::optional<int> insYOffset,
    const std::optional<int> insQualYOffset, const e2::Style& qualStyle
)
{
  for (const auto& ins : placements) {
    // modify anchor base, keeping mismatch highlight/colour
    const e2::GlobalCell anchorCell{
        {.x = fa.writeStartX +
              static_cast<int> (ins.anchorGPos - fa.writeStartXGPos),
         .y = yStart}
    };
    e2::rm_attr (anchorCell, dim);
    if (g_ui::drawAnchor) {
      e2::extend (anchorCell, insRingAbove);
    }

    if (insYOffset && yStart + *insYOffset + ins.row < fa.writeLimits.y) {
      auto writeHead = anchorCell + e2::dY (*insYOffset + ins.row);
      e2::set (writeHead, insAt);
      writeHead.x++;
      for (size_t i = 0; i < ins.len && writeHead.x < fa.writeLimits.x;
           ++i, ++writeHead.x) {
        const auto base = seq_char (readFields.seq, ins.iQuery + i);
        set (
            writeHead, base,
            readFields.seq ? e2::Style{.fg = base_colour (base)} : dim
        );
      }
    }
    else {
      // extra highlight, as insertion not shown
      e2::add_attr (anchorCell, {.fg = TB_UNDERLINE});
    }

    if (insQualYOffset &&
        yStart + *insQualYOffset + ins.row < fa.writeLimits.y) {
      auto writeHead = anchorCell + e2::dY (*insQualYOffset + ins.row);
      e2::set (writeHead, insAt);
      writeHead.x++;
      for (size_t i = 0; i < ins.len && writeHead.x < fa.writeLimits.x;
           ++i, ++writeHead.x) {
        set (
            writeHead,
            qual_char (
                readFields.qual, ins.iQuery + i, g_ui::drawSparkline
            ),
            qualStyle
        );
      }
    }
  }
}

static e2::Delta seq1 (
    const int16_t yStart, sqlite3_stmt* br_dbRow, const Seq1FrameArgs& fa
)
{
  /* draw aligned data to broswer */

  const auto& ref = g_db::locusInfo.refSlice;
  const auto pileupSpanGStart = g_db::locusInfo.start;

  // exact consumption types
  constexpr uint8_t consumesNeitherQueryOrRef = 0b00;
  constexpr uint8_t consumesQueryOnly = 0b01;
  constexpr uint8_t consumesRefOnly = 0b10;
  constexpr uint8_t consumesQueryAndRef = 0b11;

  // preconditions
  if (fa.writeStartX >= fa.writeLimits.x || yStart >= fa.writeLimits.y) {
    return {0, 0};  // no-op
  }
  TUI_ASSERT (fa.writeStartXGPos >= pileupSpanGStart, fa.writeStartXGPos);

  e2::GlobalCell writeHead{{.x = fa.writeStartX, .y = yStart}};

  const auto readFields = get_seq1_read_fields (br_dbRow);
  const int startToLEdge =
      static_cast<int> (readFields.rStart - fa.writeStartXGPos);
  if (startToLEdge > 0) {
    writeHead.x += startToLEdge;
  }

  /* --- enable optional tracks --- */
  // get ordered offsets for optional tracks.
  // Insertions first, then qual, then insertion quality.
  // Overlapping insertions take extra rows (see place_ins), so
  // offsets depend on read content, hence calculation per record.
  const auto [insPlacements, nInsRows] = place_ins (readFields, fa);
  // the read, plus insertion rows if shown
  const int nSeqRows = 1 + (g_ui::drawInsTrack ? nInsRows : 0);
  constexpr int trackYOffsetIns = 1;
  const int trackYOffsetQual = nSeqRows;
  const e2::Style qualStyle =
      !readFields.qual || g_ui::drawSparkline ? dim : e2::Style{};
  const bool drawQualTrack =
      g_ui::drawQualTrack &&
      (writeHead.y + trackYOffsetQual) < fa.writeLimits.y;
  const int trackYOffsetInsQual = trackYOffsetQual + 1;
  /* --- end enable optional tracks --- */

  clips1 (
      yStart, readFields, fa,
      drawQualTrack ? std::optional{trackYOffsetQual} : std::nullopt,
      qualStyle
  );

  auto iGc = readFields.rStart;  // genomic coordinate
  size_t iQuery = 0;
  // locus start always <= readFields.rStart
  // FOR EACH OP
  for (size_t iOp = 0; iOp < readFields.nCig; iOp++) {
    const auto op = readFields.cig_br[iOp];
    const auto opConsumeType = bam_cigar_type (bam_cigar_op (op));
    const auto opSz = bam_cigar_oplen (op);
    const auto opOnScreen = (iGc + opSz) >= fa.writeStartXGPos;

    if (!opOnScreen) {
      // skip op, incrementing indexes
      if ((opConsumeType & consumesQuery) != 0) {
        iQuery += opSz;
      }
      if ((opConsumeType & consumesRef) != 0) {
        iGc += opSz;
      }
      continue;
    }

    // At least partially on screen, to be processed
    const auto skipOffscreenBases =
        (iGc < fa.writeStartXGPos)
            ? static_cast<size_t> (fa.writeStartXGPos - iGc)
            : 0;

    if (opConsumeType == consumesQueryOnly) {
      // insertion or soft clip: drawn by ins1, clips1
      iQuery += opSz;
    }
    else if (opConsumeType == consumesRefOnly) {
      // don't advance query tracker
      // Deletions, or "skipped region"

      for (size_t i = skipOffscreenBases;
           i < opSz && writeHead.x < fa.writeLimits.x;
           ++i, ++writeHead.x) {
        set (writeHead, 'x', {.fg = TB_UNDERLINE});
        // if (drawQualTrack) {
        //   set (writeHead + e2::dY (trackYOffsetQual), 'x');
        // }
      }
      iGc += opSz;
    }
    else if (opConsumeType == consumesQueryAndRef) {
      auto opLenRemain = opSz;
      iQuery += skipOffscreenBases;
      opLenRemain -= skipOffscreenBases;

      // draw tracks
      const auto iRefOp =
          static_cast<size_t> (iGc - pileupSpanGStart) +
          skipOffscreenBases;  // ref index of first drawn base
      for (size_t i = 0; i < opLenRemain && writeHead.x < fa.writeLimits.x;
           ++i, ++writeHead.x) {
        e2::Style dispStyle;
        auto dispChar = seq_char (readFields.seq, iQuery + i);
        if (!readFields.seq) {
          dispStyle = dim;
        }
        // matches: dim, masked with '=' when mask on
        else if (ref &&
                 (std::toupper (static_cast<unsigned char> (dispChar)) ==
                  std::toupper (
                      static_cast<unsigned char> ((*ref)[iRefOp + i])
                  ))) {
          if (g_ui::drawMask) {
            dispChar = '=';
          }
          dispStyle = dim;
        }
        // mismatches, or no ref: colour; highlight when mask off
        else {
          dispStyle.fg = base_colour (dispChar);
          if (ref && !g_ui::drawMask) {
            dispStyle.fg |= TB_BOLD | TB_UNDERLINE;
          }
        }
        set (writeHead, dispChar, dispStyle);
        if (drawQualTrack) {
          set (
              writeHead + e2::dY (trackYOffsetQual),
              qual_char (readFields.qual, iQuery + i, g_ui::drawSparkline),
              qualStyle
          );
        }
      }

      iQuery += opLenRemain;
      iGc += opSz;
    }
    else if (opConsumeType == consumesNeitherQueryOrRef) {
      // hard clip: drawn by clips1
    }
    else {
      // FIXME: should this be an assertion? It implies data corruption
      // more than program malformation.
      TUI_UNREACHABLE ("CIGAR operation consumption type invalid");
    }
    if (writeHead.x >= fa.writeLimits.x) {
      // early exit if row full
      break;
    }
  }
  // after the main loop, as it modifies anchor bases
  ins1 (
      yStart, readFields, fa, insPlacements,
      g_ui::drawInsTrack ? std::optional{trackYOffsetIns} : std::nullopt,
      g_ui::drawInsTrack && drawQualTrack
          ? std::optional{trackYOffsetInsQual}
          : std::nullopt,
      qualStyle
  );

  // sequence rows, then their quality rows
  writeHead.y += nSeqRows * (1 + static_cast<int> (drawQualTrack));

  return {.dx = writeHead.x - fa.writeStartX, .dy = writeHead.y - yStart};
}

}  // namespace draw_aln

void draw_main_to_buffer()
{
  using enum tui_state::focus_mode_t;
  // NOTE: set order does matter,
  // since some places just overwrite
  // previous draw calls
  APB_ASSERT_MAIN_UI_VALID;
  TUI_ASSERT (g_db::locusInfo.valid(), g_db::locusInfo.to_string());
  {
    /* --- draw read viewer chrome --- */
    const auto* style = &dim;
    const auto* boxCh = &boxLight;
    if (tui_state::focus == viewerMode) {
      style = &bold;
      boxCh = &boxDouble;
    }
    const auto& bFrame = g_viewer::frame;
    set (edgeAB (bFrame), boxLight.horz, *style);
    set (edgeCD (bFrame), boxLight.horz, *style);
    set (edgeDA (bFrame), boxCh->vert, *style);
    set (edgeBC (bFrame), boxCh->vert, *style);
    set (vertexA (bFrame), boxCh->leftTop, *style);
    set (vertexB (bFrame), boxCh->rightTop, *style);

    set (body (g_viewer::headerSep), boxLight.horz, dim);
    set (first (g_viewer::headerSep), rightTConnect, dim);

    set (last (g_viewer::headerSep), leftTConnect, dim);

    e2::write_string (
        first (edgeCD (bFrame)) + e2::dX (2), last (edgeCD (bFrame)).x,
        fmt::format ("focused (Esc): {}", to_string (tui_state::focus)),
        dim
    );
  }
  {
    /* --- draw cmd chrome --- */
    const auto* style = &dim;
    const auto* boxCh = &boxLight;
    if (tui_state::focus == promptMode) {
      style = &bold;
      boxCh = &boxDouble;
    }
    const auto& cFrame = g_cmd::frame;
    set (vertexA (cFrame), boxCh->leftTop, *style);
    set (vertexB (cFrame), boxCh->rightTop, *style);
    set (vertexD (cFrame), boxCh->leftBottom, *style);
    set (vertexC (cFrame), boxCh->rightBottom, *style);

    set (body (edgeAB (cFrame)), heavy_horz_line, dim);
    set (body (edgeDA (cFrame)), boxCh->vert, *style);
    set (body (edgeBC (cFrame)), boxCh->vert, *style);

    set (
        g_cmd::promptChar, '>',
        (tui_state::focus == promptMode) ? none : dim
    );
  }

  /* --- draw query data --- */
  {
    // FIXME: doesn't change on most frames...
    /* size dynamic widgets */
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
        std::max (0, size (g_viewer::frame.xspan) - 2 - minAlnPaneWidth)
    );
    tableWidth = std::min (tableWidth, maxTableWidth);
    // No visible columns leaves nothing to lay the table pane out
    // with; fall back to the no-table-pane layout below.
    const bool showTable = g_ui::drawTable && tableWidth > 0;

    const auto& [frameX, frameY] = spans (g_viewer::frame);
    const auto& contentX = body (frameX);
    const auto& contentY = body (frameY);
    // skip header, separator.
    const auto dataY = e2::Span{first (contentY) + 2, last (contentY)};

    if (showTable) {
      const int splitAbsX = last (contentX) - tableWidth;
      e2::Span alnPaneX{first (contentX), splitAbsX};
      e2::Span tablePaneX{splitAbsX + 1, last (contentX)};

      g_viewer::alnPaneRefLine = {alnPaneX, first (contentY)};
      g_viewer::tablePaneHeaderLine = {tablePaneX, first (contentY)};
      g_viewer::alnPaneDataBox = {alnPaneX, dataY};
      g_viewer::tablePaneDataBox = {tablePaneX, dataY};
      g_viewer::vSep = {
          splitAbsX, construct_relative (frameY, 0, size (frameY))
      };

      TUI_ASSERT (
          valid (g_viewer::tablePaneHeaderLine),
          g_viewer::tablePaneHeaderLine
      );
      TUI_ASSERT (
          size (g_viewer::tablePaneHeaderLine) > 0,
          size (g_viewer::tablePaneHeaderLine)
      );
      APB_ASSERT_BOX_NONEMPTY (g_viewer::tablePaneDataBox);
    }
    else {
      g_viewer::tablePaneHeaderLine = {};  // invalid
      g_viewer::tablePaneDataBox = {};
      g_viewer::vSep = {};

      g_viewer::alnPaneRefLine = {contentX, first (contentY)};
      g_viewer::alnPaneDataBox = {contentX, dataY};
    }
    APB_ASSERT_BOX_NONEMPTY (g_viewer::alnPaneDataBox);
    /* end size dynamic widgets */

    /* configure/validate draw coordinates */
    const auto alnPaneHalfWidth = width (g_viewer::alnPaneDataBox) / 2;
    // Offset needed for the pane's left edge to reach
    // db.locusInfo.start. Can be positive when there isn't room
    // to center on pos, e.g. a locus near the start of its contig, or reads
    // that don't extend a full half-pane-width left of pos.
    // FIXME: bounds exclude clips (see draw_aln::clips1), so
    // clips at the span edges can't be panned to.
    const auto marginToPileupStart =
        g_db::locusInfo.start - g_db::locusInfo.pos + alnPaneHalfWidth;
    const auto marginToPileupEnd = std::max (
        g_db::locusInfo.end - alnPaneHalfWidth - g_db::locusInfo.pos,
        marginToPileupStart  // keep clamp's [lo, hi] non-empty
    );
    g_viewer::userPanOffset = std::clamp (
        g_viewer::userPanOffset, marginToPileupStart, marginToPileupEnd
    );
    const auto alnPaneLeftmostGPos =
        g_db::locusInfo.pos - alnPaneHalfWidth + g_viewer::userPanOffset;
    TUI_ASSERT (alnPaneLeftmostGPos >= 0, alnPaneLeftmostGPos);
    /* end coordinates */

    if (g_db::locusInfo.refSlice) {
      /* draw reference */
      const int64_t offsetToLocusStart =
          g_db::locusInfo.start - alnPaneLeftmostGPos;

      const int64_t skipRefBases =
          std::max<int64_t> (0, -offsetToLocusStart);
      const int64_t startDrawX = std::max<int64_t> (0, offsetToLocusStart);

      // per base, for colour
      const auto& refSlice = *g_db::locusInfo.refSlice;
      e2::GlobalCell refWriteHead{
          {.x = first (g_viewer::alnPaneRefLine.xspan) +
                static_cast<int> (startDrawX),
           .y = g_viewer::alnPaneRefLine.y}
      };
      for (auto i = static_cast<size_t> (skipRefBases);
           i < refSlice.size() &&
           refWriteHead.x < last (g_viewer::alnPaneRefLine.xspan);
           ++i, ++refWriteHead.x) {
        const auto base = static_cast<unsigned char> (refSlice[i]);
        e2::set (refWriteHead, base, {.fg = base_colour (base)});
      }
    }

    if (showTable) {
      draw_table::header (g_viewer::tablePaneHeaderLine, activeCols);
      draw_table::row_separators (g_viewer::tablePaneDataBox, activeCols);
      /* draw pane separator */
      set (body (g_viewer::vSep), boxLight.vert, dim);
      set (first (g_viewer::vSep), downTConnect, dim);
      set (last (g_viewer::vSep), upTConnect, dim);
    }

    /* iteratively draw query data */
    auto seqWriteHead = vertexA (g_viewer::alnPaneDataBox);
    auto seqWriteLim = vertexC (g_viewer::alnPaneDataBox) +
                       e2::dXY (1, 1);  // exclusive limit
    const draw_aln::Seq1FrameArgs seq1Frame{
        .writeStartX = static_cast<int16_t> (seqWriteHead.x),
        .writeStartXGPos = alnPaneLeftmostGPos,
        .writeLimits = seqWriteLim,
    };
    TUI_ASSERT (seq1Frame.valid(), seq1Frame.to_string());
    const draw_table::Row1FrameArgs row1Frame{
        .writeXStart = first (g_viewer::tablePaneDataBox.xspan),
        .writeXLimit = last (g_viewer::tablePaneDataBox.xspan),
        .cols = activeCols,
    };
    TUI_ASSERT (row1Frame.valid(), row1Frame.to_string());
    const bool multiAln = g_db::locusInfo.nAln > 1;
    // scroll units drawn on this page: reads, then empty-file labels
    uint16_t nUnitsDrawn = 0;
    sqlite3_reset (g_db::displayStmt);
    // bind offset and limit
    TUI_S3_EXPECT (
        sqlite3_bind_int64 (
            g_db::displayStmt, 1, g_db::displayScrollOffset
        ),
        SQLITE_OK
    );
    TUI_S3_EXPECT (
        sqlite3_bind_int64 (
            g_db::displayStmt, 2, seqWriteLim.y - seqWriteHead.y
        ),
        SQLITE_OK
    );
    if (g_db::nQueryRows > 0) {
      // file of the last heading drawn on this page
      const AlnFile* headingFile = nullptr;
      int stepRc = SQLITE_DONE;
      while ((seqWriteHead.y < seqWriteLim.y) &&
             (stepRc = sqlite3_step (g_db::displayStmt)) == SQLITE_ROW) {
        const auto id = sqlite3_column_int64 (
            g_db::displayStmt, schema::UserReadViewSelect::source_id
        );
        if (multiAln &&
            ((headingFile == nullptr) || headingFile->id != id)) {
          const auto file =
              std::ranges::find (g_db::alnFiles, id, &AlnFile::id);
          TUI_ASSERT (file != g_db::alnFiles.end(), id);
          headingFile = &*file;
          draw_source_name (seqWriteHead, seqWriteLim.x, *headingFile);
          seqWriteHead.y++;
          if (seqWriteHead.y >= seqWriteLim.y) {
            // heading drawn, no room for read
            break;
          }
        }
        const auto dHead = draw_aln::seq1 (
            static_cast<int16_t> (seqWriteHead.y), g_db::displayStmt,
            seq1Frame
        );
        if (showTable) {
          draw_table::row1 (seqWriteHead.y, g_db::displayStmt, row1Frame);
        }
        seqWriteHead.y += dHead.dy;
        ++nUnitsDrawn;
      }
      // either rows exhausted, or we ran out of space
      TUI_ASSERT (stepRc == SQLITE_DONE || stepRc == SQLITE_ROW, stepRc);
    }

    // if space avail, draw files without reads in query
    {
      const auto nReads = static_cast<int64_t> (g_db::nQueryRows);
      // scroll position relative to files without reads
      const auto fileOffset =
          std::max<int64_t> (g_db::displayScrollOffset - nReads, 0);
      auto emptyFiles = g_db::alnFiles |
                        std::views::filter ([] (const AlnFile& f) {
                          return f.nQuery == 0;
                        }) |
                        std::views::drop (fileOffset);
      for (const auto& file : emptyFiles) {
        if (seqWriteHead.y >= seqWriteLim.y) {
          break;
        }
        draw_source_name (seqWriteHead, seqWriteLim.x, file);
        seqWriteHead.y++;
        if (seqWriteHead.y >= seqWriteLim.y) {
          break;
        }
        std::string_view reason = "No reads match query";
        if (file.nTotal == 0) {
          reason = file.hasContig ? "File has no reads at locus"
                                  : "File does not contain contig";
        }
        e2::write_string (seqWriteHead, seqWriteLim.x, reason, dim);
        seqWriteHead.y++;
        ++nUnitsDrawn;
      }
    }

    g_viewer::nUnitsOnscreen = nUnitsDrawn;
    if (g_db::nQueryRows > 0) {
      /* draw crosshair */
      if (const int64_t pileupScreenXPosWide =
              first (g_viewer::alnPaneDataBox.xspan) + alnPaneHalfWidth -
              g_viewer::userPanOffset;
          pileupScreenXPosWide >= first (g_viewer::alnPaneDataBox.xspan) &&
          pileupScreenXPosWide < last (g_viewer::alnPaneDataBox.xspan)) {
        // if user has not scrolled crosshair offscreen:
        const auto pileupScreenXPos =
            static_cast<int16_t> (pileupScreenXPosWide);
        e2::VLine pileupCrosshair{
            pileupScreenXPos, g_viewer::alnPaneDataBox.yspan
        };
        add_attr (pileupCrosshair, {.fg = TB_REVERSE});
        // draw marker linking reference base and query position of pileup
        set (
            e2::GlobalCell{
                pileupScreenXPos,
                first (g_viewer::alnPaneDataBox.yspan) - 1
            },
            '|', dim
        );
      }
      /* end draw crosshair */
    }
    else if (!multiAln) {
      e2::write_string (
          seqWriteHead, seqWriteLim.x, "No reads match query", dim
      );
    }
    /* end draw query data */
  }

  {
    // draw locus label
    const auto& locusData = g_db::locusInfo;
    const auto& writeLine = extb::edgeAB (g_viewer::frame);
    auto writeHead = first (writeLine) + e2::dX (2);
    const auto writeLim = last (writeLine).x;

    const auto locusStr =
        fmt::format ("{}:{}", locusData.contig, locusData.pos);
    const auto spanStr =
        fmt::format ("{}-{}", locusData.start, locusData.end);
    // + 2 lead offset, + 3 separator/spacing; corner covered by `>`
    const auto locusInfoWidth = locusStr.size() + spanStr.size() + 5;
    if (size (writeLine) > locusInfoWidth) {
      writeHead.x += e2::write_string (writeHead, writeLim, locusStr);
      writeHead.x++;  // space
      set (writeHead, boxLight.vert, dim);
      writeHead.x += 2;  // past bar, then space
      writeHead.x += e2::write_string (writeHead, writeLim, spanStr);
    }
  }
  {
    // FIXME: u32 string write overload would be really useful (here and elsewhere).
    // ditto write string that uses space from the right.
    /* draw query banner */
    const auto& userQ = g_db::userQuery;
    std::string qWhereS = fmt::format (
        " WHERE {}: {}/{} reads ", userQ.where, g_db::nQueryRows,
        g_db::dbTotalRows
    );
    std::string qOrderS;
    if (!userQ.orderBy.empty()) {
      qOrderS = fmt::format (" ORDER BY {} ", userQ.orderBy);
    }
    // size + padding
    auto writeHead = last (g_cmd::contextLine) -
                     e2::dX (qWhereS.size() + qOrderS.size() + 1);
    writeHead.x = std::max (1, writeHead.x);
    writeHead.x += e2::write_string (
        writeHead, last (body (g_cmd::contextLine)).x, qWhereS
    );
    if (!qOrderS.empty()) {
      e2::set (writeHead, boxLight.vert);
      writeHead.x++;
      writeHead.x += e2::write_string (
          writeHead, last (g_cmd::contextLine).x, qOrderS
      );
    }
  }
  {
    e2::set (g_cmd::msgLine, boxLight.horz, dim);
    auto writeHead = first (g_cmd::msgLine) + e2::dX (10);
    e2::set (writeHead, doubleChevron, dim);
    writeHead.x++;
    e2::set (writeHead, ' ');
    writeHead.x++;
    if (!g_cmd::msgBuf.empty()) {
      e2::write_string (
          writeHead, last (g_cmd::msgLine).x,
          fmt::format ("{} ", g_cmd::msgBuf), dim
      );
    }
  }
  {
    // draw input
    e2::write_string (
        first (g_cmd::inputLine), last (g_cmd::inputLine).x,
        g_cmd::cmdBuf.buf.text
    );
    auto cursorCell = first (g_cmd::inputLine) +
                      e2::dX (static_cast<int> (g_cmd::cmdBuf.buf.curs));
    if (cursorCell.x < last (g_cmd::inputLine).x) {
      e2::add_attr (cursorCell, {.fg = TB_REVERSE});
    }
  }

  // postconditions:
  APB_ASSERT_MAIN_UI_VALID;
}

void draw_overlay_to_buffer()
{
  APB_ASSERT_OVERLAY_UI_VALID;
  TUI_ASSERT (!g_overlay::content.empty());

  const auto& box = g_overlay::contentBox;
  const auto& frame = g_overlay::frame;
  const auto& content = g_overlay::content;

  // NOTE: a nice property of the global only/
  // single surface drawing approach. Clearing
  // this layer clears everything "below".
  clear (box);
  clear (frame);

  set (edgeAB (frame), boxLight.horz);
  set (edgeBC (frame), boxLight.vert);
  set (edgeCD (frame), boxLight.horz);
  set (edgeDA (frame), boxLight.vert);

  set (vertexA (frame), boxLight.leftTop);
  set (vertexB (frame), boxLight.rightTop);
  set (vertexD (frame), boxLight.leftBottom);
  set (vertexC (frame), boxLight.rightBottom);

  const auto lnN = height (box);
  const auto maxLnOff =
      std::max (0, static_cast<int> (std::ssize (content)) - lnN);
  const auto lnOff = static_cast<size_t> (
      std::clamp (g_overlay::contentLnOffset, 0, maxLnOff)
  );

  auto writeHead = vertexA (frame) + e2::dX (1);
  auto xEnd = last (box.xspan) - 1;  // leave a gap before the border
  writeHead.x += e2::write_string (writeHead, xEnd, " ESC: close ", dim);
  writeHead.x += 3;
  if (lnN < std::ssize (content)) {
    e2::write_string (writeHead, xEnd, "(Pg)Up/Down: scroll", dim);
    if (lnOff < maxLnOff) {
      writeHead = vertexC (frame) - e2 ::dX (7);
      writeHead.x +=
          e2::write_string (writeHead, vertexC (frame).x, "scroll", dim);
      e2::set (writeHead, downArrow);
    }
  }

  auto lnY = extb::vertexA (box);
  for (int i = 0; i < lnN && i < std::ssize (content); ++i) {
    e2::write_string (lnY, xEnd, content[static_cast<size_t> (i) + lnOff]);
    ++lnY.y;
  }
}
