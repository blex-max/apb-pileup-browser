#include <format>
#include <stdexcept>
#include <unordered_map>

#include "app.hpp"
#include "extb.hpp"
#include "plog/Log.h"
#include "singleton.hpp"
#include "util.hpp"

namespace app {

using CmdResult = std::pair<bool, std::string>;
using Cmd = CmdResult(*)();

CmdResult cmd_quit () {
    auto& gdata= singleton::get<GlobalContext>().data;
    gdata.run = false;
    return {true, "Bye!"};
}

static std::unordered_map <std::string_view, Cmd> CMD_REGISTRY{
    {"q", &cmd_quit},
    {"quit", &cmd_quit}
};
CmdResult exec_cmd (std::string_view cmd_name) {
    if (auto it = CMD_REGISTRY.find(cmd_name); it != CMD_REGISTRY.end()) {
        return it->second();
    } else {
        return {false, std::format("Command \"{}\" not found!", cmd_name)};
    }
}


bool nav_global (tb_event& ev) {
    assert (ev.key);

    auto& gui = singleton::get<GlobalContext>().ui;

    switch (ev.key) {
        case TB_KEY_ENTER:
        extb::clear(gui.cmd);
        extb::clear(gui.status);
        extb::write_string (
            gui.status,
            {0, 0},
            app::exec_cmd(gui.cmd_buf).second,
            TB_DIM
        );
        gui.cmd_buf.clear();
        break;

        default:
        return false;
    }
    return true;

}

void init_pileup_display () {
    auto& main_view = singleton::get<GlobalContext>().ui.main;
    extb::clear(main_view);

    // TODO consider how to split view into primary (sequence) area and secondary (read info) area
    // "flex box style"
    // use pc.ui struct

    extb::Box ref_row {main_view.i().first, main_view.j()};
    // set separator
    extb::set_cell (extb::Box{main_view.i().first + 1, main_view.j()}, 0x2500, TB_DIM);

    extb::Box query_box {{main_view.i().first + 2, main_view.i().last - 1}, main_view.j()};
    extb::Box info_row {main_view.i().last, main_view.j()};  // show e.g. coordinates

    // TODO add these elements to pileup ui sub struct (consider: as optionals?)
}


// void draw_sequence_data () {
    
// }


int nav_browser (tb_event& ev) {
    assert (ev.key);

    auto& pc = singleton::get<PileupContext>();
    auto& pstate = pc.state;
    auto& pui = pc.ui;

    switch (ev.key) {
        // N.B. selection not really important
        // for mvp - only scrolling of queries really needed
        // (might use > instead, or just no selector for now)
        case TB_KEY_ARROW_DOWN:
        rm_attr(pui.seq, {0, pstate.row_sel}, TB_REVERSE);
        ++pstate.row_sel;
        pstate.row_sel = std::clamp(pstate.row_sel, 0, last_local_i(pui.seq));
        add_attr(pui.seq, {0, pstate.row_sel}, TB_REVERSE);
        break;

        case TB_KEY_ARROW_UP:
        rm_attr(pui.seq, {0, pstate.row_sel}, TB_REVERSE);
        --pstate.row_sel;
        pstate.row_sel = std::clamp(pstate.row_sel, 0, last_local_i(pui.seq));
        add_attr(pui.seq, {0, pstate.row_sel}, TB_REVERSE);
        break;

        default:
        return false;
    }
    return true;
}


void handle_input (tb_event& ev) {
    assert (ev.ch);

    auto& gui = singleton::get<GlobalContext>().ui;

    extb::clear(gui.cmd);
    gui.cmd_buf.append(1, ev.ch);
    extb::write_string(gui.cmd, {0, 0}, gui.cmd_buf);
}


void init_global_ui () {
    auto& gui = singleton::get<GlobalContext>().ui;

    tb_clear();
    // boxes with closed coordinates
    extb::Box screen {
        {0, tb_height() - 1},
        {0, tb_width() - 1}
    };
    if (!screen.valid()) {
        // TODO this should probably warn and allow user to correct
        throw std::runtime_error ("Invalid screen area, terminal likely too small");
    }
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
        throw std::runtime_error ("Could not calculate valid screen area");
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

    gui.main = display_box;
    gui.cmd = input_row;
    gui.cmd_caret = caret;
    gui.status = status_row;
}


void init () {
    setlocale(LC_ALL, "");

    tb_init();
    tb_clear();

    singleton::init<GlobalContext>();
    try {
        init_global_ui();
    } catch (const std::exception &e) {
        throw make_runtime_error("Error initialising view: {}", e.what());
    }

    auto& gui = singleton::get<GlobalContext>().ui;
    write_string(gui.status, {0, 0}, "Hello!", TB_DIM);

    tb_present();
}


void loop () {
    tb_event ev{};

    auto& gdata = singleton::get<GlobalContext>().data;

    while (gdata.run) {
        tb_poll_event (&ev);
        if (ev.type == TB_EVENT_RESIZE)
        {
            try {
                init_global_ui();
            } catch (const std::exception &e) {
                throw make_runtime_error (
                    "Error while attempting to resize view: {}", e.what()
                );
            }
        }
        switch (gdata.state)
        {
            case (app::app_state::browse):
            // render_pileup (ctx);  // TODO
            if (ev.key)
            {
                // fallthrough until true with short circuit
                // NOTE: this may be fine - an alternative:
                // bool handled = nav_1() || nav_2()...
                // if (!handled) { nav global }
                nav_browser (ev) ||
                nav_global (ev);
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
            handle_input (ev);
        }

        if (gdata.debug)
        {
          // pass
        }

        PLOGD << std::format ("Processed frame {}", gdata.frame);
        ++gdata.frame;
        tb_present();
    }
}


// TODO
// void state_transition (from, to, Context& ctx);
// void init_browse_state (Context& ctx);


}
