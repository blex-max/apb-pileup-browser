#include <format>
#include <stdexcept>

#include "plog/Log.h"

extern "C" {
    #include "termbox2.h"
}
#include "extb.hpp"

#include "app.hpp"
#include "ctx.hpp"
#include "hts/boundary-types.hpp"
#include "util.hpp"
#include "GlobalContext.hpp"
#include "PileupContext.hpp"
#include "cmd.hpp"

void shutdown () {
    tb_shutdown();
}

void render_input_text () {
    auto& gui = ctx::get<GlobalContext>().ui;
    extb::clear(gui.cmd);
    extb::write_string(gui.cmd, {0, 0}, gui.cmd_buf.text);
}


bool nav_global (tb_event& ev) {
    assert (ev.key);

    auto& gui = ctx::get<GlobalContext>().ui;

    switch (ev.key) {
        case TB_KEY_ENTER:
        extb::clear(gui.cmd);
        extb::clear(gui.status);
        extb::write_string (
            gui.status,
            {0, 0},
            cmd::exec_cmd(gui.cmd_buf.text).msg,
            TB_DIM
        );
        input::clear(gui.cmd_buf);
        break;

        case TB_KEY_BACKSPACE:
        case TB_KEY_BACKSPACE2:
        input::del_back(gui.cmd_buf);
        render_input_text();
        break;

        default:
        return false;
    }
    return true;

}


void draw_sequence_data () {
    auto& pctx = ctx::get<PileupContext>();
    const auto& pdat = pctx.data;  // TODO
    auto& pui = pctx.ui;
    const auto& query_box = pui.base_display.query_box;
    const auto& ref_line = pui.base_display.ref_line;
    const auto& status_line = pui.base_display.status_line;

    const auto& pd = pdat.pd; // pileup display data

    extb::write_string(ref_line, {0, 0}, std::get<RefRep> (pd).s, 0);

    const auto &queries = std::get<Queries> (pd);
    for (size_t i = 0; i < queries.size(); ++i) {
        const auto q = queries[i];
        write_string (
            query_box,
            {static_cast<int> (i), static_cast<int>(q.start)},
            q.s
        );
    }

    add_attr(query_box, {0, pdat.row_sel}, TB_REVERSE);
}


void init_pileup_display () {
    auto& pctx = ctx::get<PileupContext>();
    auto& pui = pctx.ui;
    auto& global_ui_main = ctx::get<GlobalContext>().ui.main;
    extb::clear(global_ui_main);

    const auto display_i = global_ui_main.i();
    const extb::Span display_j{global_ui_main.j().first, static_cast<int>(ceil (global_ui_main.j().last * pui.ui_frac_display))};

    pui.base_display.ref_line = {display_i.first, display_j};
    // set ref separator
    extb::set_cell (extb::Box {display_i.first + 1, display_j}, 0x2500, TB_DIM);
    // set vertical separator
    extb::set_cell (extb::Box {display_i, display_j.last + 1}, 0x2502, TB_DIM);

    pui.base_display.query_box = {{display_i.first + 2, display_i.last - 2}, display_j};
    // set status separator
    extb::set_cell (extb::Box {display_i.last - 1, display_j}, 0x2500, TB_DIM);
    pui.base_display.status_line = {display_i.last, display_j};  // show e.g. coordinates

    draw_sequence_data(); // TODO
}


int nav_browser (tb_event& ev) {
    assert (ev.key);

    auto& pc = ctx::get<PileupContext>();
    auto& pstate = pc.data;
    auto& pui = pc.ui;
    auto& query_box = pui.base_display.query_box;

    switch (ev.key) {
        // N.B. selection not really important
        // for mvp - only scrolling of queries really needed
        // (might use > instead, or just no selector for now)
        case TB_KEY_ARROW_DOWN:
        rm_attr(query_box, {pstate.row_sel, 0}, TB_REVERSE);
        ++pstate.row_sel;
        pstate.row_sel = std::clamp(pstate.row_sel, 0, last_local_i(query_box));
        add_attr(query_box, {pstate.row_sel, 0}, TB_REVERSE);
        break;

        case TB_KEY_ARROW_UP:
        rm_attr(query_box, {pstate.row_sel, 0}, TB_REVERSE);
        --pstate.row_sel;
        pstate.row_sel = std::clamp(pstate.row_sel, 0, last_local_i(query_box));
        add_attr(query_box, {pstate.row_sel, 0}, TB_REVERSE);
        break;

        default:
        return false;
    }
    return true;
}



void init_global_ui () {
    auto& gui = ctx::get<GlobalContext>().ui;

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
        {main_box.i().first + 1, main_box.i().last},
        {main_box.j().first + 1, main_box.j().last - 1}
    };

    gui.main = display_box;
    gui.cmd = input_row;
    gui.cmd_caret = caret;
    gui.status = status_row;
}

void enter_pileup_mode () {
    auto& pctx = ctx::get<PileupContext>();

    // TODO if (gctx.demo) ... {
    pctx.data.pd = make_test_display_data(10, 20);
    // }

    init_pileup_display();
}

void enter_state () {
    const auto& state = ctx::get<GlobalContext>().data.state;
    switch (state) {
        case app_state::browse:
        enter_pileup_mode();
        break;

        default:
        return;
    }
}


void init () {
    PLOGD << "Begin global init";
    setlocale(LC_ALL, "");

    tb_init();
    tb_set_input_mode(TB_INPUT_ESC); // | TB_INPUT_MOUSE for mouse ev
    tb_clear();

    ctx::init<GlobalContext>();

    try {
        init_global_ui();
    } catch (const std::exception &e) {
        throw make_runtime_error("Error initialising view: {}", e.what());
    }

    auto& gui = ctx::get<GlobalContext>().ui;
    write_string(gui.status, {0, 0}, "Hello!", TB_DIM);

    // init modal contexts
    ctx::init<PileupContext>();

    enter_state();

    tb_present();
    PLOGD << "Global init complete";
}


void loop () {
    tb_event ev{};

    auto& gctx = ctx::get<GlobalContext>();
    auto& gui = gctx.ui;
    auto& gdata = gctx.data;

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
            case (app_state::browse):
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

        // global
        if (ev.ch)
        {
            input::insert(gui.cmd_buf, ev.ch);
            render_input_text();
        }

        // print requested debug info
        // NOTE: doesn't clear
        int j = 0;
        for (const auto& pair : gdata.debug) {
            const auto msg = pair.second();
            const auto ncharw = extb::write_string({0, j}, 0, msg);
            if (ncharw < msg.size()) {
                break;
            }
            j += ncharw;
        }

        PLOGD << std::format ("Processed frame {}", gdata.frame);
        ++gdata.frame;
        tb_present();
    }
}


// TODO
// void init_browse_state (Context& ctx);
