#include "widgets.hpp"

#include "frontend/extb/extb.hpp"
#include "frontend/extb/widgets/box.hpp"
#include "plog/Log.h"
#include "shared/err.hpp"

VoidOrErr calc_widgets (TopUI& ui)
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
  auto& pileupWgt = ui.main;
  auto& cmdWgt = ui.cmd;

  pileupWgt.frame = e2::Box{mainI, screenJ};
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

  return {};
}

VoidOrErr draw_widgets (AppState& state)
{
  PLOGD << "Drawing widgets";

  auto& pWgt = state.ui.main;
  auto& cWgt = state.ui.cmd;

  auto& cFrame = cWgt.frame;
  set (top_left (cFrame), 0x256D);
  set (top_right (cFrame), 0x256E);
  set (bottom_left (cFrame), 0x2570);
  set (bottom_right (cFrame), 0x256F);
  // top border
  // set (e2::JLine {first (cFrame.ispan), section (cFrame.jspan, 1, size (cFrame.jspan) - 1)}, 0x2500);

  set (cWgt.inputCaret, ':');
  set (
      section (cWgt.sepLine, 1, size (cWgt.sepLine) - 1), 0x2500,
      TB_DIM
  );
  // set (first(cWgt.sepLine), 0x251C);
  // set (last(cWgt.sepLine), 0x2524);

  e2::write_string (
      first (cWgt.inputLine), last (cWgt.inputLine).j,
      cWgt.inputBuf.text
  );
  e2::write_string (
      first (cWgt.msgLine), last (cWgt.msgLine).j, cWgt.msgBuf
  );

  return {};
}
