#include <cstdlib>
#include <iostream>
#include <locale.h>
#include <stdlib.h>

#define TB_IMPL

#include "layout.hpp"
#include "tb.hpp"


constexpr auto CMD_H = 6;

int main(int, char **) {
    setlocale(LC_ALL, "");

    tb_init();
    tb_clear();

    // BOXES WITH CLOSED COORDINATES
    lo::ClosedBox screen{0, tb_width() - 1, 0, tb_height() - 1};
    lo::ClosedBox seq_box{
      screen.x1,
      screen.x2,
      screen.y1,
      screen.y2 - CMD_H
    };
    lo::ClosedBox cmd_box{
      screen.x1,
      screen.x2,
      seq_box.y2 + 1,
      seq_box.y2 + CMD_H
    };

    // draw top-level boxes
    {
      // seq display
      // corners
      extb::set_cell({seq_box.x1, seq_box.y1}, 0x256D);
      extb::set_cell({seq_box.x2, seq_box.y1}, 0x256E);
      extb::set_cell({seq_box.x1, seq_box.y2}, 0x2570);
      extb::set_cell({seq_box.x2, seq_box.y2}, 0x256F);

      // top, bottom
      for (auto x = seq_box.x1 + 1; x < seq_box.x2; ++x) {
        extb::set_cell({x, seq_box.y1}, 0x2500);
      }
      for (auto x = seq_box.x1 + 1; x < seq_box.x2; ++x) {
        extb::set_cell({x, seq_box.y2}, 0x2500);
      }

      // sides
      for (auto y = seq_box.y1 + 1; y < seq_box.y2; ++y) {
        extb::set_cell({seq_box.x1, y}, 0x2502);
        extb::set_cell({seq_box.x2, y}, 0x2502);
      }

      // cmd display
      // corners
      extb::set_cell({cmd_box.x1, cmd_box.y1}, 0x256D);
      extb::set_cell({cmd_box.x2, cmd_box.y1}, 0x256E);
      extb::set_cell({cmd_box.x1, cmd_box.y2}, 0x2570);
      extb::set_cell({cmd_box.x2, cmd_box.y2}, 0x256F);

      // top, bottom
      for (auto x = cmd_box.x1 + 1; x < cmd_box.x2; ++x) {
        extb::set_cell({x, cmd_box.y1}, 0x2500);
      }
      for (auto x = cmd_box.x1 + 1; x < cmd_box.x2; ++x) {
        extb::set_cell({x, cmd_box.y2}, 0x2500);
      }

      // sides
      for (auto y = cmd_box.y1 + 1; y < cmd_box.y2; ++y) {
        extb::set_cell({cmd_box.x1, y}, 0x2502);
        extb::set_cell({cmd_box.x2, y}, 0x2502);
      }

      // input line
      extb::set_cell({cmd_box.x1 + 1, cmd_box.y1 + 2}, ':');
      // return sep
      extb::set_cell({cmd_box.x1, cmd_box.y1 + 3}, 0x251C);
      extb::set_cell({cmd_box.x2, cmd_box.y1 + 3}, 0x2524);
      for (auto x = cmd_box.x1 + 1; x < cmd_box.xend() - 1; ++x) {
        extb::set_cell({x, cmd_box.y1 + 3}, 0x2500) | extb::dim;
      }
    }

    // cmd sub areas
    lo::ClosedBox history_box {
      cmd_box.x1 + 1,      // exclude border
      cmd_box.x2 - 1,      // exclude border
      cmd_box.y1 + 1,      // exclude border
      cmd_box.y1 + 1       // single row
    };
    lo::ClosedBox input_box{
      cmd_box.x1 + 2,      // exclude border, caret :
      cmd_box.x2 - 1,      // exclude border
      history_box.y2 + 1,  // line after history
      history_box.y2 + 1   // single row
    };
    lo::ClosedBox return_box{
      cmd_box.x1 + 1,      // exclude border
      cmd_box.x2 - 1,      // exclude border
      input_box.y2 + 2,    // line after input, skip sep
      input_box.y2 + 1     // single row
    };

    tb_present();

    tb_event ev{};
    tb_poll_event(&ev);  // blocking

    tb_shutdown();
    return EXIT_SUCCESS;
}
