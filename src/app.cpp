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

}
