#include <format>
#include <unordered_map>

#include "app.hpp"
#include "extb.hpp"

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
        extb::clear(ui.cmd);
        extb::clear(ui.status);
        extb::write_string (
            ui.status,
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
        rm_attr(ui.main, {0, mode_ctx.row_sel}, TB_REVERSE);
        ++mode_ctx.row_sel;
        mode_ctx.row_sel = std::clamp(mode_ctx.row_sel, 0, ui.main.ilocal().last);
        add_attr(ui.main, {0, mode_ctx.row_sel}, TB_REVERSE);
        break;

        case TB_KEY_ARROW_UP:
        rm_attr(ui.main, {0, mode_ctx.row_sel}, TB_REVERSE);
        --mode_ctx.row_sel;
        mode_ctx.row_sel = std::clamp(mode_ctx.row_sel, 0, ui.main.ilocal().last);
        add_attr(ui.main, {0, mode_ctx.row_sel}, TB_REVERSE);
        break;

        default:
        return nav_global (ev, ctx);
    }
    return true;
}


void handle_input (tb_event& ev, Context& ctx) {
    assert (ev.ch);

    auto& ui = ctx.ui;

    extb::clear(ui.cmd);
    ui.cmd_buf.append(1, ev.ch);
    extb::write_string(ui.cmd, {0, 0}, ui.cmd_buf);
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


void draw_global (Context& ctx) {
    // BOXES WITH CLOSED COORDINATES
    // TODO must figure out resize immediately!
    auto screen = extb::Box::make_box ({0, tb_height() - 1}, {0, tb_width() - 1});
    auto main_box = extb::Box::make_box (
        {screen.iglobal().first, screen.iglobal().last - CMD_H - STATUS_H},
        screen.jglobal()
    );
    auto cmd_box = extb::Box::make_box (
        {main_box.iglobal().last + 1, main_box.iglobal().last + CMD_H},
        screen.jglobal()
    );
    auto status_box = extb::Box::make_box (
        {cmd_box.iglobal().last + 1, cmd_box.iglobal().last + STATUS_H},  // last inclusive row
        screen.jglobal()
    );

    // draw fixed elements (carets, borders)
    // seq display
    // top corners
    extb::set_cell ({main_box.iglobal().first, main_box.jglobal().first}, 0x256D);
    extb::set_cell ({main_box.iglobal().first, main_box.jglobal().last}, 0x256E);

    // sides
    for (auto i = main_box.iglobal().first + 1; i <= main_box.iglobal().last; ++i) {
        extb::set_cell ({i, main_box.jglobal().first}, 0x2502);
        extb::set_cell ({i, main_box.jglobal().last}, 0x2502);
    }

    // top
    for (auto j =  main_box.jglobal().first + 1; j < main_box.jglobal().last; ++j) {
        extb::set_cell ({main_box.iglobal().first, j}, 0x2500);
    }

    // cmd display
    // corners
    extb::set_cell ({cmd_box.iglobal().first, cmd_box.jglobal().first}, 0x256D);
    extb::set_cell ({cmd_box.iglobal().first, cmd_box.jglobal().last}, 0x256E);
    extb::set_cell ({cmd_box.iglobal().last, cmd_box.jglobal().last}, 0x256F);
    extb::set_cell ({cmd_box.iglobal().last, cmd_box.jglobal().first}, 0x2570);

    // sides
    for (auto i = cmd_box.iglobal().first + 1; i < cmd_box.iglobal().last; ++i) {
        extb::set_cell ({i, cmd_box.jglobal().first}, 0x2502);
        extb::set_cell ({i, cmd_box.jglobal().last}, 0x2502);
    }

    // top, bottom
    for (auto j = cmd_box.jglobal().first + 1; j < cmd_box.jglobal().last; ++j) {
        extb::set_cell ({cmd_box.iglobal().first, j}, 0x2500);
        extb::set_cell ({cmd_box.iglobal().last, j}, 0x2500);
    }

    // status display
    extb::set_cell ({status_box.iglobal().first, status_box.jglobal().first}, 0x256D);
    extb::set_cell ({status_box.iglobal().first, status_box.jglobal().last}, 0x256E);
    extb::set_cell ({status_box.iglobal().last, status_box.jglobal().last}, 0x256F);
    extb::set_cell ({status_box.iglobal().last, status_box.jglobal().first}, 0x2570);

    // sides
    for (auto i = status_box.iglobal().first + 1; i < status_box.iglobal().last; ++i) {
        extb::set_cell ({i, status_box.jglobal().first}, 0x2502);
        extb::set_cell ({i, status_box.jglobal().last}, 0x2502);
    }

    // top, bottom
    for (auto j = status_box.jglobal().first + 1; j < status_box.jglobal().last; ++j) {
        extb::set_cell ({status_box.iglobal().first, j}, 0x2500);
        extb::set_cell ({status_box.iglobal().last, j}, 0x2500);
    }

    // cmd input
    auto input_row =
        extb::Box::make_row (
            cmd_box.iglobal().first + 1,
            {
                cmd_box.jglobal().first + 2,      // skip border, leave space for caret :
                cmd_box.jlocal().last - 1      // exclude border
            }
        );
    extb::Cell caret{cmd_box.iglobal().first + 1, cmd_box.jglobal().first + 1};
    extb::set_cell(caret, ':', TB_DIM);
    auto status_row =
        extb::make_sub_row (
            status_box,
            1,
            {
                1,
                status_box.jlocal().last - 1
            }
        );
    write_string(status_row, {0, 0}, "Hello!", TB_DIM);

    // for data display
    auto display_box =
        extb::make_sub_box (
            main_box,
            {1, main_box.ilocal().last - 1},
            {1, main_box.jlocal().last - 1}
        );

    ctx.ui.main = display_box;
    ctx.ui.cmd = input_row;
    ctx.ui.cmd_caret = caret;
    ctx.ui.status = status_row;
}


Context& init () {
    setlocale(LC_ALL, "");

    tb_init();
    tb_clear();

    auto& ctx = Context::create();
    draw_global(ctx);

    // TODO call global draw

    tb_present();

    return ctx;
}


// TODO
// void state_transition (from, to, Context& ctx);
// void init_browse_state (Context& ctx);


}
