#include "widgets.hpp"

#include <cmath>

#include "app/state.hpp"
#include "frontend/drawing_chars.hpp"
#include "frontend/extb/extb.hpp"
#include "frontend/extb/widgets/box.hpp"
#include "plog/Log.h"
#include "shared/err.hpp"

VoidOrErr calc_widgets (TopUI& ui, const AppConfig& conf)
{
  PLOGD << "Calculating widget size";

  e2::Span screenI{0, tb_height()};
  e2::Span screenJ{0, tb_width()};

  // vertical sectioning of terminal
  e2::Span mainI{screenI.first, screenI.last - CMD_H};
  e2::Span cmdI{mainI.last, mainI.last + CMD_H};

  PLOGD << "screen i last: " << screenI.last;
  PLOGD << "cmd i first: " << cmdI.first;
  PLOGD << "cmd i last: " << cmdI.last;

  if (!e2::valid (screenI) || !e2::valid (screenJ) ||
      !e2::valid (mainI) || !e2::valid (cmdI)) {
    return std::unexpected{make_internal_err (
        "Could not calculate widgets. Terminal likley too small!"
    )};
  }

  // widgets to calc
  auto& pWgt = ui.main;
  pWgt.frame = e2::Box{mainI, screenJ};

  auto vSplitJ = static_cast<int> (
      ceil ((size (screenJ) - 2) * conf.query_box_frac)
  );
  pWgt.vSep = {section (mainI, 0, size (mainI) - 1), vSplitJ};
  pWgt.refLine = {
      first (mainI) + 1, section (screenJ, 1, vSplitJ)
  };
  pWgt.headerLine = {
      first (mainI) + 1, {vSplitJ + 1, last (screenJ) - 1}
  };
  pWgt.headerSep = {
      first (mainI) + 2, screenJ
  };  // overlapping, to set connectors
  pWgt.queryBox = {
      section (mainI, 3, size (mainI) - 1),
      section (screenJ, 1, vSplitJ)
  };
  pWgt.dataBox = {
      section (mainI, 3, size (mainI) - 1),
      {vSplitJ + 1, last (screenJ) - 1}
  };
  pWgt.querySep = {mainI.last - 2, screenJ};

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

  return {};
}

void draw_chrome (TopUI& ui)
{
  PLOGD << "Drawing widgets";

  auto& cWgt = ui.cmd;

  auto& cFrame = cWgt.frame;
  set (top_left (cFrame), ch::topLeftRoundCorner, TB_DIM);
  set (top_right (cFrame), ch::topRightRoundCorner, TB_DIM);
  set (bottom_left (cFrame), ch::bottomLeftRoundCorner, TB_DIM);
  set (
      bottom_right (cFrame), ch::bottomRightRoundCorner, TB_DIM
  );

  set (
      e2::JLine{first (cFrame.ispan), body (cFrame.jspan)},
      ch::horzHeavy, TB_DIM
  );
  set (
      e2::ILine{body (cFrame.ispan), first (cFrame.jspan)},
      ch::vertLine, TB_DIM
  );
  set (
      e2::ILine{body (cFrame.ispan), last (cFrame.jspan) - 1},
      ch::vertLine, TB_DIM
  );
  // line for displaying current filter (i.e. last query made)
  // e.g | WHERE: <...> | ORDER BY: <...> |
  set (body (cWgt.statusSep), ch::horzLine, TB_DIM);

  set (cWgt.inputCaret, ':');
  set (body (cWgt.sepLine), ch::horzLine, TB_DIM);

  auto& pWgt = ui.main;
  auto& pFrame = pWgt.frame;

  set (top_left (pFrame), ch::topLeftRoundCorner, TB_DIM);
  set (top_right (pFrame), ch::topRightRoundCorner, TB_DIM);

  set (
      e2::JLine{
          first (pFrame.ispan),
          section (pFrame.jspan, 1, size (pFrame.jspan) - 1)
      },
      ch::horzLine, TB_DIM
  );
  set (
      e2::ILine{
          section (pFrame.ispan, 1, size (pFrame.ispan)),
          first (pFrame.jspan)
      },
      ch::vertLine, TB_DIM
  );
  set (
      e2::ILine{
          section (pFrame.ispan, 1, size (pFrame.ispan)),
          last (pFrame.jspan) - 1
      },
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

void draw_content (AppState& state)
{
  auto& cWgt = state.ui.cmd;
  e2::write_string (
      first (cWgt.inputLine), last (cWgt.inputLine).j,
      cWgt.inputBuf.text
  );
  e2::write_string (
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

  e2::write_string (
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
  draw_chrome (state.ui);
  draw_content (state);

  PLOGD << "Drawing widgets";
}
