#include "widgets.hpp"

#include <fmt/format.h>

#include <cmath>

#include "app/screen_projection.hpp"
#include "app/state.hpp"
#include "frontend/drawing_chars.hpp"
#include "frontend/extb/box/box.hpp"
#include "frontend/extb/extb.hpp"
#include "plog/Log.h"
#include "shared/err.hpp"

// precondition: ui.main.frame is set
VoidOrErr calc_pileup_child_widgets (
    PileupWgt& pWgt, const AppConfig& conf
)
{
  PLOGD << "Calculating pileup child panes";

  const auto& frame = pWgt.frame;
  if (width (frame) <= 1 || height (frame) <= 1) {
    return std::unexpected (make_internal_err (
        "Could not calculate pileup panes. Terminal likely too "
        "small!"
    ));
  }

  const auto& pWgtISpan = pWgt.frame.ispan;
  const auto& pWgtJSpan = pWgt.frame.jspan;

  auto vSplitJ = static_cast<int> (ceil (
      static_cast<double> (size (pWgtJSpan) - 2) *
      conf.query_box_frac
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

  return {};
}

VoidOrErr calc_all_widgets (TopUI& ui, const AppConfig& conf)
{
  PLOGD << "Calculating widget size";

  const auto screenH = tb_height();
  const auto screenW = tb_width();
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

  const auto pcRet = calc_pileup_child_widgets (pWgt, conf);
  if (!pcRet) {
    return std::unexpected (pcRet.error());
  }

  auto& cWgt = ui.cmd;
  cWgt.frame = e2::Box{cmdI, screenJ};

  // TODO possibly richer subdivision
  cWgt.queryStatusLine = e2::JLine{
      cmdI.first + 1,
      {
          screenJ.first + 1,  // skip border
          screenJ.last - 1  // exclude border
      }
  };
  cWgt.statusSep = e2::JLine{
      cmdI.first + 2,  // skip input
      {
          screenJ.first, screenJ.last
      }  // include frame, to draw pipe connectors at line ends
  };

  // cmd input
  cWgt.inputLine = e2::JLine{
      cmdI.first + 3,
      {
          screenJ.first +
              2,  // skip border, leave space for caret :
          screenJ.last - 1  // exclude border
      }
  };
  cWgt.inputCaret =
      e2::GlobalCell{cmdI.first + 3, screenJ.first + 1};

  cWgt.sepLine = e2::JLine{
      cmdI.first + 4,  // skip input
      {
          screenJ.first, screenJ.last
      }  // include frame, to draw pipe connectors at line ends
  };

  cWgt.msgLine = e2::JLine{
      cmdI.first + 5,  // skip input, separator line
      {screenJ.first + 1, screenJ.last - 1}
  };

  auto& helpWgt = ui.help;
  const auto helpH = static_cast<int> (
      std::floor (static_cast<double> (screenH) * 0.6)
  );
  const auto helpW = static_cast<int> (
      std::floor (static_cast<double> (screenW) * 0.6)
  );
  const auto iOff =
      static_cast<int> (std::floor ((screenH - helpH) / 2));
  const auto jOff =
      static_cast<int> (std::floor ((screenW - helpW) / 2));

  e2::Span hISpan{iOff, iOff + helpH};
  e2::Span hJSpan{jOff, jOff + helpW};

  helpWgt.frame = e2::Box{hISpan, hJSpan};
  helpWgt.contentBox = e2::Box{body (hISpan), body (hJSpan)};

  return {};
}

static void draw_static_chrome (TopUI& ui)
{
  PLOGD << "Drawing widgets";

  auto& cWgt = ui.cmd;

  auto& cFrame = cWgt.frame;
  set (nw_vertex (cFrame), ch::topLeftRoundCorner, TB_DIM);
  set (ne_vertex (cFrame), ch::topRightRoundCorner, TB_DIM);
  set (sw_vertex (cFrame), ch::bottomLeftRoundCorner, TB_DIM);
  set (se_vertex (cFrame), ch::bottomRightRoundCorner, TB_DIM);

  set (body (north_edge (cFrame)), ch::horzHeavy, TB_DIM);
  set (body (west_edge (cFrame)), ch::vertLine, TB_DIM);
  set (body (east_edge (cFrame)), ch::vertLine, TB_DIM);
  // line for displaying current filter (i.e. last query made)
  // e.g | WHERE: <...> | ORDER BY: <...> |
  set (body (cWgt.statusSep), ch::horzLine, TB_DIM);

  set (cWgt.inputCaret, ':');
  set (body (cWgt.sepLine), ch::horzLine, TB_DIM);

  auto& pWgt = ui.main;
  auto& pFrame = pWgt.frame;

  set (nw_vertex (pFrame), ch::topLeftRoundCorner, TB_DIM);
  set (ne_vertex (pFrame), ch::topRightRoundCorner, TB_DIM);

  set (body (north_edge (pFrame)), ch::horzLine, TB_DIM);
  // main frame is open at the bottom (the cmd frame closes it), so the
  // side edges skip only the top corner and run to the last row.
  set (
      section (west_edge (pFrame), 1, height (pFrame)),
      ch::vertLine, TB_DIM
  );
  set (
      section (east_edge (pFrame), 1, height (pFrame)),
      ch::vertLine, TB_DIM
  );

  set (body (pWgt.headerSep), ch::horzLine, TB_DIM);
  set (first (pWgt.headerSep), ch::rightTConnect, TB_DIM);

  set (body (pWgt.querySep), ch::horzLine, TB_DIM);
  set (first (pWgt.querySep), ch::rightTConnect, TB_DIM);
  set (last (pWgt.querySep), ch::leftTConnect, TB_DIM);

  set (body (pWgt.vSep), ch::vertLine, TB_DIM);
  set (first (pWgt.vSep), ch::downTConnect, TB_DIM);
  set (last (pWgt.vSep), ch::upTConnect, TB_DIM);

  set (last (pWgt.headerSep), ch::leftTConnect, TB_DIM);
}

static void draw_dynamic_content (AppState& state)
{
  auto& pWgt = state.ui.main;

  if (state.locus.refSlice) {
    const auto& locus = state.locus;
    auto proj = project_onto_box (
        locus.pos, size (pWgt.refLine), locus.start
    );

    e2::write_ascii_string (
        {pWgt.refLine.i,
         first (pWgt.refLine.jspan) + proj.jOffset},
        last (pWgt.refLine.jspan),
        locus.refSlice->substr (proj.skipChars)
    );
  }

  // locus info
  {
    const auto& locus = state.locus;
    int jCurs = 1;  // initial space
    const auto lineStart = first (pWgt.infoLine);
    const auto lineEnd = last (pWgt.infoLine.jspan);
    jCurs += e2::write_ascii_string (
        translate (lineStart, e2::J (jCurs)), lineEnd,
        "LOCUS:", TB_DIM
    );
    jCurs++;  // space
    jCurs += e2::write_ascii_string (
        translate (lineStart, e2::J (jCurs)), lineEnd,
        fmt::format ("{}:{}", locus.contig, locus.pos)
    );
    jCurs++;  // space
    set (
        translate (lineStart, e2::J (jCurs++)), ch::vertLine,
        TB_DIM
    );
    jCurs++;  // space
    jCurs += e2::write_ascii_string (
        translate (lineStart, e2::J (jCurs)), lineEnd,
        "SPAN:", TB_DIM
    );
    jCurs++;  // space
    jCurs += e2::write_ascii_string (
        translate (lineStart, e2::J (jCurs)), lineEnd,
        fmt::format ("{}-{}", locus.start, locus.end)
    );
    jCurs++;  // space
    set (
        translate (lineStart, e2::J (jCurs++)), ch::vertLine,
        TB_DIM
    );
  }

  auto& cWgt = state.ui.cmd;
  e2::write_ascii_string (
      first (cWgt.inputLine), last (cWgt.inputLine).j,
      cWgt.inputBuf.text
  );
  auto cursorCell = translate (
      first (cWgt.inputLine),
      e2::J (static_cast<int> (cWgt.inputBuf.curs))
  );
  if (cursorCell.j < last (cWgt.inputLine).j) {
    e2::add_attr (cursorCell, TB_REVERSE);
  }
  e2::write_ascii_string (
      first (cWgt.msgLine), last (cWgt.msgLine).j, cWgt.msgBuf,
      TB_DIM
  );

  // stringify query
  auto& userClause = state.query.userClause;
  std::string userClauseString;

  if (!userClause.where.empty()) {
    userClauseString.append ("WHERE ");
    for (size_t i = 0; i < userClause.where.size(); ++i) {
      userClauseString.append (userClause.where[i]);
      if (i != (userClause.where.size() - 1)) {
        userClauseString.append (" ");
      }
    }
  }

  if (!userClause.orderBy.empty()) {
    userClauseString.append (" ORDER BY ");
    userClauseString.append (userClause.orderBy);
  }

  e2::write_ascii_string (
      first (cWgt.queryStatusLine),
      last (cWgt.queryStatusLine).j, userClauseString, TB_DIM
  );
}

void draw_widgets (AppState& state)
{
  // NOTE: set order does matter,
  // since some places just overwrite
  // previous draw calls

  // NOTE: Worry about border stylisation last!
  draw_static_chrome (state.ui);
  draw_dynamic_content (state);

  PLOGD << "Drawing widgets";
}
