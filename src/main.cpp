#include <algorithm>
#include <cstdlib>
#include <locale.h>
#include <stdlib.h>

#define TB_IMPL
extern "C" {
  #include "termbox2.h"
}

#include "tb.hpp"
#include "hts/boundary-types.hpp"
#include "app.hpp"


constexpr auto CMD_H = 3;  // inc. borders
constexpr auto STATUS_H = 3;

int main(int, char **) {
    // --- DEMO DATA --- //
    // n.b. probaby good to keep a demo mode in the final product!

    const auto dd = make_test_display_data(10);

    // --- END --- //

    setlocale(LC_ALL, "");

    tb_init();
    tb_clear();

    // BOXES WITH CLOSED COORDINATES
    auto screen = extb::Box::make_box (0, tb_width() - 1, 0, tb_height() - 1);
    auto browse_box = extb::Box::make_box (
        screen.gx1,
        screen.gx2,
        screen.gy1,
        screen.gy2 - CMD_H - STATUS_H
    );
    auto cmd_box = extb::Box::make_box (
        screen.gx1,
        screen.gx2,
        browse_box.gy2 + 1,
        browse_box.gy2 + CMD_H  // last inclusive y
    );
    auto status_box = extb::Box::make_box (
        screen.gx1,
        screen.gx2,
        cmd_box.gy2 + 1,
        cmd_box.gy2 + STATUS_H // last inclusive y
    );

    // draw fixed elements (carets, borders)
    // seq display
    // corners
    extb::set_cell({browse_box.gx1, browse_box.gy1}, 0x256D);
    extb::set_cell({browse_box.gx2, browse_box.gy1}, 0x256E);

    // top, bottom
    for (auto x = browse_box.gx1 + 1; x < browse_box.gx2; ++x) {
        extb::set_cell({x, browse_box.gy1}, 0x2500);
    }

    // sides
    for (auto y = browse_box.gy1 + 1; y < browse_box.gy2 + 1; ++y) {
        extb::set_cell({browse_box.gx1, y}, 0x2502);
        extb::set_cell({browse_box.gx2, y}, 0x2502);
    }

    // cmd display
    // corners
    extb::set_cell({cmd_box.gx1, cmd_box.gy1}, 0x256D);
    extb::set_cell({cmd_box.gx2, cmd_box.gy1}, 0x256E);
    extb::set_cell({cmd_box.gx1, cmd_box.gy2}, 0x2570);
    extb::set_cell({cmd_box.gx2, cmd_box.gy2}, 0x256F);

    // top, bottom
    for (auto x = cmd_box.gx1 + 1; x < cmd_box.gx2; ++x) {
        extb::set_cell({x, cmd_box.gy1}, 0x2500);
        extb::set_cell({x, cmd_box.gy2}, 0x2500);
    }

    // sides
    for (auto y = cmd_box.gy1 + 1; y < cmd_box.gy2; ++y) {
        extb::set_cell({cmd_box.gx1, y}, 0x2502);
        extb::set_cell({cmd_box.gx2, y}, 0x2502);
    }

    // status display
    extb::set_cell({status_box.gx1, status_box.gy1}, 0x256D);
    extb::set_cell({status_box.gx2, status_box.gy1}, 0x256E);
    extb::set_cell({status_box.gx1, status_box.gy2}, 0x2570);
    extb::set_cell({status_box.gx2, status_box.gy2}, 0x256F);

    // top, bottom
    for (auto x = status_box.gx1 + 1; x < status_box.gx2; ++x) {
        extb::set_cell({x, status_box.gy1}, 0x2500);
        extb::set_cell({x, status_box.gy2}, 0x2500);
    }

    // sides
    for (auto y = status_box.gy1 + 1; y < status_box.gy2; ++y) {
        extb::set_cell({status_box.gx1, y}, 0x2502);
        extb::set_cell({status_box.gx2, y}, 0x2502);
    }

    // cmd input
    std::string input_buf;
    auto input_line = extb::Box::make_box (
        cmd_box.gx1 + 2,      // leave space for caret :
        cmd_box.gx2 - 1,      // exclude border
        cmd_box.gy1 + 1,
        cmd_box.gy1 + 1
    );
    extb::Point caret{cmd_box.gx1 + 1, cmd_box.gy1 + 1};
    extb::set_cell(caret, ':', TB_DIM);

    std::string status_buf;
    auto status_line = extb::Box::make_box (
        status_box.gx1 + 1,
        status_box.gx2 - 1,
        status_box.gy1 + 1,
        status_box.gy1 + 1
    );

    // writeable area
    auto seq_box = extb::Box::make_box (
        browse_box.gx1 + 1,
        browse_box.gx2 - 1,
        browse_box.gy1 + 1,
        browse_box.gy2 - 1
    );

    int row_sel = 0;
    const auto &queries = std::get<Queries> (dd);
    for (size_t i = 0; i < queries.size(); ++i) {
        write_string(seq_box, {0, static_cast<int>(i)}, queries[i].q);
    }
    add_attr(seq_box, {0, row_sel}, TB_REVERSE);

    write_string(status_line, {0, 0}, "Hello!", TB_DIM);

    tb_present();

    // input struct
    tb_event ev{};
    // modal state machine
    // TODO access ui elements and so on via context object
    app::Context ctx{
        {
            seq_box,
            input_line,
            status_line
        }  // ui elements
    };

    while (ctx.run) {
        tb_poll_event(&ev);  // blocking
        switch (ctx.state) {
            case (app::app_state::browse):
                switch (ev.key) {
                    // N.B. selection not really important
                    // for mvp - only scrolling of queries really needed
                    // (might use > instead, or just no selector for now)
                    case TB_KEY_ARROW_DOWN:
                        rm_attr(seq_box, {0, row_sel}, TB_REVERSE);
                        ++row_sel;
                        row_sel = std::clamp(row_sel, 0, seq_box.ylast);
                        add_attr(seq_box, {0, row_sel}, TB_REVERSE);
                        break;
                    case TB_KEY_ARROW_UP:
                        rm_attr(seq_box, {0, row_sel}, TB_REVERSE);
                        --row_sel;
                        row_sel = std::clamp(row_sel, 0, seq_box.ylast);
                        add_attr(seq_box, {0, row_sel}, TB_REVERSE);
                        break;
                    case TB_KEY_ENTER:
                        {
                            auto cmd_report = app::exec_cmd(input_buf, ctx);
                            input_buf.clear();
                            extb::clear(input_line);
                            extb::clear(status_line);
                            extb::write_string (status_line, {0, 0}, cmd_report.second, TB_DIM);
                        }
                        break;
                    default:
                        break;
                }
                // type input
                if (ev.ch) {
                    input_buf.append(1, ev.ch);
                    extb::write_string(input_line, {0, 0}, input_buf);
                }
                break;
            default:  // global
              break;
            }
        if (ctx.debug) {
          // pass
        }
        tb_present();
        ++ctx.frame;
    }

    tb_shutdown();
    return EXIT_SUCCESS;
}
