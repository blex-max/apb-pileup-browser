#include <format>
#include <stdexcept>
#include <unordered_map>

#include "app.hpp"
#include "extb.hpp"
#include "util.hpp"

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

int nav_browser (tb_event& ev, Context& ctx) {
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
        mode_ctx.row_sel = std::clamp(mode_ctx.row_sel, 0, last_local_i(ui.main));
        add_attr(ui.main, {0, mode_ctx.row_sel}, TB_REVERSE);
        break;

        case TB_KEY_ARROW_UP:
        rm_attr(ui.main, {0, mode_ctx.row_sel}, TB_REVERSE);
        --mode_ctx.row_sel;
        mode_ctx.row_sel = std::clamp(mode_ctx.row_sel, 0, last_local_i(ui.main));
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


void set_global_ui (Context& ctx) {
    tb_clear();
    // boxes with closed coordinates
    extb::Box screen {
        {0, tb_height() - 1},
        {0, tb_width() - 1}
    };
    extb::Box main_box {
        {screen.i().first, screen.i().last - CMD_H - STATUS_H},
        screen.j()
    };
    extb::Box cmd_box {
        {main_box.i().last + 1, main_box.i().last + CMD_H},
        screen.j()
    };
    extb::Box status_box {
        {cmd_box.i().last + 1, cmd_box.i().last + STATUS_H},  // last inclusive row
        screen.j()
    };
    if (!screen.valid() || !main_box.valid() || !cmd_box.valid() || !status_box.valid()) {
        throw std::runtime_error ("failed to draw global ui");
    }

    // draw fixed elements (carets, borders)
    // main display
    // top corners
    extb::set_cell ({main_box.i().first, main_box.j().first}, 0x256D);
    extb::set_cell ({main_box.i().first, main_box.j().last}, 0x256E);

    // sides
    for (auto i = main_box.i().first + 1; i <= main_box.i().last; ++i) {
        extb::set_cell ({i, main_box.j().first}, 0x2502);
        extb::set_cell ({i, main_box.j().last}, 0x2502);
    }

    // top
    for (auto j =  main_box.j().first + 1; j < main_box.j().last; ++j) {
        extb::set_cell ({main_box.i().first, j}, 0x2500);
    }

    // cmd display
    // corners
    extb::set_cell ({cmd_box.i().first, cmd_box.j().first}, 0x256D);
    extb::set_cell ({cmd_box.i().first, cmd_box.j().last}, 0x256E);
    extb::set_cell ({cmd_box.i().last, cmd_box.j().last}, 0x256F);
    extb::set_cell ({cmd_box.i().last, cmd_box.j().first}, 0x2570);

    // sides
    for (auto i = cmd_box.i().first + 1; i < cmd_box.i().last; ++i) {
        extb::set_cell ({i, cmd_box.j().first}, 0x2502);
        extb::set_cell ({i, cmd_box.j().last}, 0x2502);
    }

    // top, bottom
    for (auto j = cmd_box.j().first + 1; j < cmd_box.j().last; ++j) {
        extb::set_cell ({cmd_box.i().first, j}, 0x2500);
        extb::set_cell ({cmd_box.i().last, j}, 0x2500);
    }

    // status display
    extb::set_cell ({status_box.i().first, status_box.j().first}, 0x256D);
    extb::set_cell ({status_box.i().first, status_box.j().last}, 0x256E);
    extb::set_cell ({status_box.i().last, status_box.j().last}, 0x256F);
    extb::set_cell ({status_box.i().last, status_box.j().first}, 0x2570);

    // sides
    for (auto i = status_box.i().first + 1; i < status_box.i().last; ++i) {
        extb::set_cell ({i, status_box.j().first}, 0x2502);
        extb::set_cell ({i, status_box.j().last}, 0x2502);
    }

    // top, bottom
    for (auto j = status_box.j().first + 1; j < status_box.j().last; ++j) {
        extb::set_cell ({status_box.i().first, j}, 0x2500);
        extb::set_cell ({status_box.i().last, j}, 0x2500);
    }

    // cmd input
    extb::Box input_row {
        cmd_box.i().first + 1,
        {
            cmd_box.j().first + 2,      // skip border, leave space for caret :
            cmd_box.j().last - 1        // exclude border
        }
    };
    extb::Cell caret{cmd_box.i().first + 1, cmd_box.j().first + 1};
    extb::set_cell(caret, ':', TB_DIM);

    extb::Box status_row {
        status_box.i().first + 1,
        {
            status_box.j().first + 1,
            status_box.j().last - 1
        }
    };

    // for data display
    extb::Box display_box {
        {main_box.i().first + 1, main_box.i().last - 1},
        {main_box.j().first + 1, main_box.j().last - 1}
    };

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
    try {
        set_global_ui(ctx);
    } catch (const std::exception &e) {
        throw;
    }
    write_string(ctx.ui.status, {0, 0}, "Hello!", TB_DIM);

    // TODO call global draw

    tb_present();

    return ctx;
}


void loop (Context& ctx) {
    tb_event ev{};

    auto& global = ctx.global;
    auto& ui = ctx.ui;

    while (global.run) {
        tb_poll_event (&ev);
        if (ev.type == TB_EVENT_RESIZE)
        {
            try {
                set_global_ui(ctx);
            } catch (const std::exception &e) {
                throw;
            }
        }
        // should rerender each frame to account for resize changes
        switch (global.state)
        {
            case (app::app_state::browse):
            // render_pileup (ctx);  // TODO
            if (ev.key)
            {
                nav_browser (ev, ctx);
            }
            // if (global.state != app::app_state::browse) {
            //     // transition
            // }
            break;

            default:  // global
            break;
        }

        if (ev.ch)
        {
            handle_input (ev, ctx);
        }

        if (global.debug)
        {
          // pass
        }

        ++global.frame;
        tb_present();
    }
}


// TODO
// void state_transition (from, to, Context& ctx);
// void init_browse_state (Context& ctx);


}
