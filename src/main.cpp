#include <cstdlib>
#include <locale.h>
#include <stdlib.h>

#define TB_IMPL
#include "app.hpp"
#include "hts/boundary-types.hpp"

constexpr auto CMD_H = 3;  // inc. borders
constexpr auto STATUS_H = 3;

int main(int, char **) {
    // --- DEMO DATA --- //
    // n.b. probaby good to keep a demo mode in the final product!

    const auto dd = make_test_display_data(10, 20);

    // --- END --- //

    setlocale(LC_ALL, "");

    tb_init();
    tb_clear();

    // BOXES WITH CLOSED COORDINATES
    auto screen = extb::Box::make_box (0, tb_width() - 1, 0, tb_height() - 1);
    auto main_box = extb::Box::make_box (
        screen.gx1,
        screen.gx2,
        screen.gy1,
        screen.gy2 - CMD_H - STATUS_H
    );
    auto cmd_box = extb::Box::make_box (
        screen.gx1,
        screen.gx2,
        main_box.gy2 + 1,
        main_box.gy2 + CMD_H  // last inclusive y
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
    extb::set_cell({main_box.gx1, main_box.gy1}, 0x256D);
    extb::set_cell({main_box.gx2, main_box.gy1}, 0x256E);

    // top, bottom
    for (auto x = main_box.gx1 + 1; x < main_box.gx2; ++x) {
        extb::set_cell({x, main_box.gy1}, 0x2500);
    }

    // sides
    for (auto y = main_box.gy1 + 1; y < main_box.gy2 + 1; ++y) {
        extb::set_cell({main_box.gx1, y}, 0x2502);
        extb::set_cell({main_box.gx2, y}, 0x2502);
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
    auto data_box = extb::Box::make_box (
        main_box.gx1 + 1,
        main_box.gx2 - 1,
        main_box.gy1 + 1,
        main_box.gy2 - 1
    );

    const auto &queries = std::get<Queries> (dd);
    for (size_t i = 0; i < queries.size(); ++i) {
        const auto q = queries[i];
        write_string (
            data_box,
            {static_cast<int> (q.start), static_cast<int> (i)},
            queries[i].q
        );
    }
    int row_sel = 0;
    add_attr(data_box, {0, row_sel}, TB_REVERSE);

    write_string(status_line, {0, 0}, "Hello!", TB_DIM);

    tb_present();

    app::Context ctx{
        .ui = {
            data_box,
            input_line,
            status_line
        }
    };

    app::loop(ctx);

    tb_shutdown();
    return EXIT_SUCCESS;
}
