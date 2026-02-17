#include <format>
#include <unordered_map>

#include "app.hpp"

namespace app {

using CmdResult = std::pair<bool, std::string>;
using Cmd = CmdResult(*)(Context&);

CmdResult cmd_quit (Context& ctx) {
    ctx.global.run = false;
    return {true, "Bye!"};
}

static std::unordered_map <std::string_view, Cmd> CMD_REGISTRY{
    {"q", &cmd_quit},
    {"quit", &cmd_quit}
};
CmdResult exec_cmd (std::string_view cmd_name, Context& ctx) {
    if (auto it = CMD_REGISTRY.find(cmd_name); it != CMD_REGISTRY.end()) {
        return it->second(ctx);
    } else {
        return {false, std::format("Command \"{}\" not found!", cmd_name)};
    }
}


bool nav_global (tb_event& ev, Context& ctx) {
    assert (ev.key);

    auto& ui = ctx.ui;

    switch (ev.key) {
        case TB_KEY_ENTER:
        extb::clear(ui.input_line);
        extb::clear(ui.status_line);
        extb::write_string (
            ui.status_line,
            {0, 0},
            app::exec_cmd(ui.cmd_buf, ctx).second,
            TB_DIM
        );
        ui.cmd_buf.clear();
        break;

        default:
        return false;
    }
    return true;

}

bool nav_browser (tb_event& ev, Context& ctx) {
    assert (ev.key);

    auto& ui = ctx.ui;
    auto& mode_ctx = ctx.browse_ctx;

    switch (ev.key) {
        // N.B. selection not really important
        // for mvp - only scrolling of queries really needed
        // (might use > instead, or just no selector for now)
        case TB_KEY_ARROW_DOWN:
        rm_attr(ui.display, {0, mode_ctx.row_sel}, TB_REVERSE);
        ++mode_ctx.row_sel;
        mode_ctx.row_sel = std::clamp(mode_ctx.row_sel, 0, ui.display.ylast);
        add_attr(ui.display, {0, mode_ctx.row_sel}, TB_REVERSE);
        break;

        case TB_KEY_ARROW_UP:
        rm_attr(ui.display, {0, mode_ctx.row_sel}, TB_REVERSE);
        --mode_ctx.row_sel;
        mode_ctx.row_sel = std::clamp(mode_ctx.row_sel, 0, ui.display.ylast);
        add_attr(ui.display, {0, mode_ctx.row_sel}, TB_REVERSE);
        break;

        default:
        return nav_global (ev, ctx);
    }
    return true;
}


void handle_input (tb_event& ev, Context& ctx) {
    assert (ev.ch);

    auto& ui = ctx.ui;

    extb::clear(ui.input_line);
    ui.cmd_buf.append(1, ev.ch);
    extb::write_string(ui.input_line, {0, 0}, ui.cmd_buf);
}


void loop (Context& ctx) {
    tb_event ev{};

    auto& global = ctx.global;
    auto& ui = ctx.ui;

    while (global.run) {
        tb_poll_event (&ev);
        switch (global.state) {
            case (app::app_state::browse):
            if (ev.key)
            {
                nav_browser (ev, ctx);
            }
            else if (ev.ch)
            {
                handle_input (ev, ctx);
            }
            // if (global.state != app::app_state::browse) {
            //     // transition
            // }
            break;

            default:  // global
            break;
        }

        if (global.debug) {
          // pass
        }
        ++global.frame;
        tb_present();
    }
}


Context& init () {
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
    auto input_line = extb::Box::make_box (
        cmd_box.gx1 + 2,      // leave space for caret :
        cmd_box.gx2 - 1,      // exclude border
        cmd_box.gy1 + 1,
        cmd_box.gy1 + 1
    );
    extb::Point caret{cmd_box.gx1 + 1, cmd_box.gy1 + 1};
    extb::set_cell(caret, ':', TB_DIM);
    auto status_line = extb::Box::make_box (
        status_box.gx1 + 1,
        status_box.gx2 - 1,
        status_box.gy1 + 1,
        status_box.gy1 + 1
    );
    write_string(status_line, {0, 0}, "Hello!", TB_DIM);

    // for data display
    auto display_box = extb::Box::make_box (
        main_box.gx1 + 1,
        main_box.gx2 - 1,
        main_box.gy1 + 1,
        main_box.gy2 - 1
    );

    tb_present();

    return Context::create(display_box, caret, input_line, status_line);
}


void state_transition (Context& ctx);


void init_browse_state (Context& ctx);


}
