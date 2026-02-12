#include <cstdlib>
#include <locale.h>
#include <stdlib.h>

#define TB_IMPL

#include "layout.hpp"
#include "tb.hpp"
#include "hts/boundary-types.hpp"


constexpr auto CMD_H = 6;  // inc. borders

enum class states : uint8_t {
  cmd,
  browse,
  global
};


int main(int, char **) {
    // --- DEMO DATA --- //
    // n.b. probaby good to keep a demo mode in the final product!

    const auto dd = make_test_display_data(10);

    // --- END --- //

    setlocale(LC_ALL, "");

    tb_init();
    tb_clear();

    // BOXES WITH CLOSED COORDINATES
    lay::ClosedBox screen{0, tb_width() - 1, 0, tb_height() - 1};
    // excluding border, i.e. writable area
    lay::ClosedBox browse_box{
      screen.x1,
      screen.x2,
      screen.y1,
      screen.y2 - CMD_H
    };
    lay::ClosedBox cmd_box{
      screen.x1,
      screen.x2,
      browse_box.y2 + 1,
      browse_box.y2 + CMD_H  // last inclusive y
    };

    // draw fixed elements (carets, borders)
    // seq display
    // corners
    extb::set_cell({browse_box.x1, browse_box.y1}, 0x256D);
    extb::set_cell({browse_box.x2, browse_box.y1}, 0x256E);
    extb::set_cell({browse_box.x1, browse_box.y2}, 0x2570);
    extb::set_cell({browse_box.x2, browse_box.y2}, 0x256F);

    // top, bottom
    for (auto x = browse_box.x1 + 1; x < browse_box.x2; ++x) {
      extb::set_cell({x, browse_box.y1}, 0x2500);
      extb::set_cell({x, browse_box.y2}, 0x2500);
    }

    // sides
    for (auto y = browse_box.y1 + 1; y < browse_box.y2; ++y) {
      extb::set_cell({browse_box.x1, y}, 0x2502);
      extb::set_cell({browse_box.x2, y}, 0x2502);
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
      extb::set_cell({x, cmd_box.y2}, 0x2500);
    }

    // sides
    for (auto y = cmd_box.y1 + 1; y < cmd_box.y2; ++y) {
      extb::set_cell({cmd_box.x1, y}, 0x2502);
      extb::set_cell({cmd_box.x2, y}, 0x2502);
    }

    // cmd sub areas
    lay::ClosedBox history_box {
      cmd_box.x1 + 1,
      cmd_box.x2 - 1,
      cmd_box.y1 + 1,
      cmd_box.y1 + 1       // single row
    };
    // input line
    auto caret = extb::set_cell({cmd_box.x1 + 1, cmd_box.y1 + 2}, ':');
    lay::ClosedBox input_box{
      cmd_box.x1 + 2,      // exclude caret :
      cmd_box.x2 - 1,      // exclude border
      history_box.y2 + 1,  // line after history
      history_box.y2 + 1   // single row
    };
    // return sep
    extb::set_cell({cmd_box.x1, input_box.y2 + 1}, 0x251C);
    extb::set_cell({cmd_box.x2, input_box.y2 + 1}, 0x2524);
    for (auto x = cmd_box.x1 + 1; x < cmd_box.x2; ++x) {
      extb::set_cell({x, input_box.y2 + 1}, 0x2500) | extb::dim;
    }
    lay::ClosedBox return_box{
      cmd_box.x1 + 1,
      cmd_box.x2 - 1,
      input_box.y2 + 2,    // line after input, skip sep
      input_box.y2 + 2     // single row
    };


    // writeable area
    lay::ClosedBox seq_box{
      browse_box.x1 + 1,
      browse_box.x2 - 1,
      browse_box.y1 + 1,
      browse_box.y2 - 1
    };

    const auto &queries = std::get<Queries> (dd);
    for (size_t i = 0; i < queries.size(); ++i) {
      seq_box.write_string({0, static_cast<int>(i)}, queries[i].q);
      // seq_box.set_local({0, static_cast<int>(i)}, 'a');
    }

    tb_present();

    // input struct
    tb_event ev{};
    // modal state machine
    auto state = states::global;
    bool run = true;
    bool debug = true;
    uint32_t frame = 0;
    while (run) {
      tb_poll_event(&ev);  // blocking
      switch (state) {
        case (states::browse):
          break;
        case (states::cmd):
          break;
        default:  // global
          break;
      }
      if (debug) {
        // test
        browse_box.set_local({0, 0}, 't');
      }
      if (ev.ch == 'q') {
        run = false;
      }
      tb_present();
      ++frame;
    }

    tb_shutdown();
    return EXIT_SUCCESS;
}
