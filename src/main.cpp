#include <algorithm>
#include <cstdlib>
#include <locale.h>
#include <stdlib.h>

#define TB_IMPL

#include "layout.hpp"
#include "tb.hpp"
#include "hts/boundary-types.hpp"


constexpr auto CMD_H = 3;  // inc. borders
constexpr auto STATUS_H = 3;

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
      screen.y2 - CMD_H - STATUS_H
    };
    lay::ClosedBox cmd_box{
      screen.x1,
      screen.x2,
      browse_box.y2 + 1,
      browse_box.y2 + CMD_H  // last inclusive y
    };
    lay::ClosedBox status_box{
      screen.x1,
      screen.x2,
      cmd_box.y2 + 1,
      cmd_box.y2 + STATUS_H // last inclusive y
    };

    // draw fixed elements (carets, borders)
    // seq display
    // corners
    extb::set_cell({browse_box.x1, browse_box.y1}, 0x256D);
    extb::set_cell({browse_box.x2, browse_box.y1}, 0x256E);

    // top, bottom
    for (auto x = browse_box.x1 + 1; x < browse_box.x2; ++x) {
      extb::set_cell({x, browse_box.y1}, 0x2500);
    }

    // sides
    for (auto y = browse_box.y1 + 1; y < browse_box.y2 + 1; ++y) {
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

    // status display
    extb::set_cell({status_box.x1, status_box.y1}, 0x256D);
    extb::set_cell({status_box.x2, status_box.y1}, 0x256E);
    extb::set_cell({status_box.x1, status_box.y2}, 0x2570);
    extb::set_cell({status_box.x2, status_box.y2}, 0x256F);

    // top, bottom
    for (auto x = status_box.x1 + 1; x < status_box.x2; ++x) {
      extb::set_cell({x, status_box.y1}, 0x2500);
      extb::set_cell({x, status_box.y2}, 0x2500);
    }

    // sides
    for (auto y = status_box.y1 + 1; y < status_box.y2; ++y) {
      extb::set_cell({status_box.x1, y}, 0x2502);
      extb::set_cell({status_box.x2, y}, 0x2502);
    }

    // cmd input
    std::string input_buf;
    lay::ClosedBox input_line{
      cmd_box.x1 + 2,      // leave space for caret :
      cmd_box.x2 - 1,      // exclude border
      cmd_box.y1 + 1,
      cmd_box.y1 + 1
    };
    extb::Point caret{cmd_box.x1 + 1, cmd_box.y1 + 1};
    extb::set_cell(caret, ':', TB_DIM);

    std::string status_buf;
    lay::ClosedBox status_line{
      status_box.x1 + 1,
      status_box.x2 - 1,
      status_box.y1 + 1,
      status_box.y1 + 1
    };

    // writeable area
    lay::ClosedBox seq_box{
      browse_box.x1 + 1,
      browse_box.x2 - 1,
      browse_box.y1 + 1,
      browse_box.y2 - 1
    };

    int row_sel = 0;
    const auto &queries = std::get<Queries> (dd);
    for (size_t i = 0; i < queries.size(); ++i) {
      seq_box.write_string({0, static_cast<int>(i)}, queries[i].q);
    }
    seq_box.add_attr({0, row_sel}, TB_REVERSE);

    status_line.write_string({0, 0}, "Hello!", 0, TB_DIM);

    tb_present();

    // input struct
    tb_event ev{};
    // modal state machine
    auto state = states::browse;
    bool run = true;
    bool debug = true;
    uint32_t frame = 0;
    while (run) {
      tb_poll_event(&ev);  // blocking
      switch (state) {
        case (states::browse):
          // check ctrl first
          switch (ev.key) {
            // N.B. selection not really important
            // for mvp - only scrolling really needed
            case TB_KEY_ARROW_DOWN:
              seq_box.rm_attr({0, row_sel}, TB_REVERSE);
              ++row_sel;
              row_sel = std::clamp(row_sel, 0, seq_box.ylast());
              seq_box.add_attr({0, row_sel}, TB_REVERSE);
              break;
            case TB_KEY_ARROW_UP:
              seq_box.rm_attr({0, row_sel}, TB_REVERSE);
              --row_sel;
              row_sel = std::clamp(row_sel, 0, seq_box.ylast());
              seq_box.add_attr({0, row_sel}, TB_REVERSE);
              break;
            default:
              break;
          }

          // type input
          if (ev.ch) {
            input_buf.append(1, ev.ch);
            input_line.write_string({0, 0}, input_buf);
          }
          break;
        default:  // global
          break;
      }
      if (debug) {
        // test
        // browse_box.set_cell({0, 0}, 't');
      }
      if (ev.key == TB_KEY_CTRL_Q) {
        run = false;
      }
      tb_present();
      ++frame;
    }

    tb_shutdown();
    return EXIT_SUCCESS;
}
