#include "widgets.hpp"

#include <cmath>

#include "app/state.hpp"
#include "frontend/box_drawing_chars.hpp"
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
  auto& cmdWgt = ui.cmd;
  cmdWgt.frame = e2::Box{cmdI, screenJ};

  {
    // cmd input
    cmdWgt.inputLine = e2::JLine{
        cmdI.first + 1,
        {
            screenJ.first +
                2,  // skip border, leave space for caret :
            screenJ.last - 1  // exclude border
        }
    };
    cmdWgt.inputCaret =
        e2::GlobalCell{cmdI.first + 1, screenJ.first + 1};

    cmdWgt.sepLine = e2::JLine{
        cmdI.first + 2,  // skip input
        {
            screenJ.first, screenJ.last
        }  // include frame, to draw pipe connectors at line ends
    };

    cmdWgt.msgLine = e2::JLine{
        cmdI.first + 3,  // skip input, separator line
        {screenJ.first + 1, screenJ.last - 1}
    };
  }

  auto& pileupWgt = ui.main;
  pileupWgt.frame = e2::Box{mainI, screenJ};

  auto vSplitJ = static_cast<int> (
      ceil ((size (screenJ) - 2) * conf.query_box_frac)
  );
  pileupWgt.vSep = {
      section (mainI, 0, size (mainI) - 1), vSplitJ
  };
  pileupWgt.refLine = {
      first (mainI) + 1, section (screenJ, 1, vSplitJ)
  };
  pileupWgt.refSep = {
      first (mainI) + 2, section (screenJ, 0, vSplitJ + 1)
  };  // overlapping, to set connectors
  pileupWgt.queryBox = {
      section (mainI, 3, size (mainI) - 1),
      section (screenJ, 1, vSplitJ)
  };
  pileupWgt.querySep = {mainI.last - 2, screenJ};

  return {};
}

VoidOrErr draw_widgets (AppState& state)
{
  // NOTE: set order does matter,
  // since some places just overwrite
  // previous draw calls

  // NOTE: Worry about border stylisation last!

  PLOGD << "Drawing widgets";

  auto& cWgt = state.ui.cmd;

  auto& cFrame = cWgt.frame;
  set (top_left (cFrame), ch::topLeftRoundCorner, TB_DIM);
  set (top_right (cFrame), ch::topRightRoundCorner, TB_DIM);
  set (bottom_left (cFrame), ch::bottomLeftRoundCorner, TB_DIM);
  set (
      bottom_right (cFrame), ch::bottomRightRoundCorner, TB_DIM
  );

  // TODO: add line for displaying current filter (i.e. last query made)
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

  set (cWgt.inputCaret, ':');
  set (body (cWgt.sepLine), ch::horzLine, TB_DIM);

  e2::write_string (
      first (cWgt.inputLine), last (cWgt.inputLine).j,
      cWgt.inputBuf.text
  );
  e2::write_string (
      first (cWgt.msgLine), last (cWgt.msgLine).j, cWgt.msgBuf,
      TB_DIM
  );

  auto& pWgt = state.ui.main;
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

  set (body (pWgt.refSep), ch::horzLine, TB_DIM);
  set (first (pWgt.refSep), ch::rightTConnect, TB_DIM);

  set (body (pWgt.querySep), ch::horzLine, TB_DIM);
  set (first (pWgt.querySep), ch::rightTConnect, TB_DIM);
  set (last (pWgt.querySep), ch::leftTConnect, TB_DIM);

  set (body (pWgt.vSep), ch::vertLine, TB_DIM);
  set (first (pWgt.vSep), ch::downTConnect, TB_DIM);
  set (last (pWgt.vSep), ch::upTConnect, TB_DIM);

  set (last (pWgt.refSep), ch::leftTConnect, TB_DIM);

  return {};
}
