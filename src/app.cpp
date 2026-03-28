#include <format>
#include <stdexcept>

#include "plog/Log.h"

extern "C" {
    #include "termbox2.h"
}
#include "extb.hpp"

#include "table.hpp"
#include "app.hpp"
#include "ctx.hpp"
#include "hts/boundary-types.hpp"
#include "util.hpp"
#include "GlobalContext.hpp"
#include "PileupContext.hpp"
#include "cmd.hpp"
#include "demo.hpp"
#include "hts/accessors.hpp"

void shutdown () {
    tb_shutdown();
}

void render_input_text () {
    auto& gui = ctx::get<GlobalContext>().ui;
    extb::clear_box (gui.cmd);
    extb::write_string_within (
        extb::to_global (gui.cmd, {0, 0}),
        gui.cmd,
        gui.cmd_buf.text
    );
}

void draw_sequence_data () {
    PLOGD << "Begin draw for sequence data";

    const auto& pctx = ctx::get<PileupContext>();
    const auto& pconf = pctx.config;
    const auto& pdat = pctx.data;  // TODO
    const auto& pui = pctx.ui;
    const auto& data_box = pui.data_box;
    const auto& query_box = pui.query_box;
    const auto& ref_line = pui.ref_line;
    const auto& status_line = pui.status_line;

    extb::clear_box (query_box);
    extb::clear_box (ref_line);
    extb::clear_box (status_line);

    /* setup for extracting user requested display fields for tabular display */

    const auto nprop = pconf.bam_props_request.size();

    std::vector<std::vector<std::string>> prop_cols;  // TODO flat vector with stride rather than nested vectors
    std::vector<std::string> prop_headers;
    std::vector<StringifyFn> prop_callbacks;

    prop_cols.reserve(nprop);
    prop_headers.reserve(nprop);
    prop_callbacks.reserve(nprop);

    for (const auto& head : pconf.bam_props_request) {
        const auto& it = BAM_RENDER_CALLBACKS.find(head); 
        if (it == BAM_RENDER_CALLBACKS.end()) {
            PLOGW << std::format("Unknown property callback {}", head);
            continue;
        }

        prop_callbacks.push_back(it->second);
        prop_headers.push_back(head);
        prop_cols.emplace_back();
    }

    assert (prop_callbacks.size() == prop_cols.size());
    assert (prop_cols.size() == prop_headers.size());

    /* setup for mapping pileup coordinates to view coordinates */

    const auto query_box_jspan = jspan (query_box);
    size_t query_box_w = query_box_jspan.size();
    auto seq_browser_local_j_center = (query_box_w / 2);
    auto pileup_gpos = pdat.ps.pos;
    auto pileup_gstart = pdat.ps.gstart;
    int leftmost_visible_gpos = pileup_gpos - seq_browser_local_j_center;
    auto seq_browser_j_center = query_box_jspan.first + seq_browser_local_j_center;
    // auto rightmost_visible_gpos = pileup_gpos + query_box_w - seq_browser_local_j_center;

    // auto gpos2local = [pileup_gstart] (int global_pos) { return global_pos - pileup_gstart; };


    PLOGD << "drawing ref";

    // NOTE: genomic_substr may fall down later
    // with indels and such. Could make it cigar aware!
    const auto visible_ref_seq =
        genomic_substr(pileup_gstart, leftmost_visible_gpos, query_box_w, pdat.ref.s);
    extb::write_string_within (
        extb::to_global(ref_line, {0, 0}),
        ref_line,
        visible_ref_seq
    );


    PLOGD << "drawing queries";
    const auto p1arr = pdat.query.begin.get();
    const auto np1 = pdat.query.n;
    for (size_t i = 0; i < np1; ++i) {
        const auto& p1 = p1arr + i;

        /*
            If query begins before displayed region,
            we need to subset the string to keep it
            aligned with the displayed reference.
            If it overruns to the right,
            write_string will just discard those chars.
        */

        int edge_to_qstart= htsacc::gstart (p1) - leftmost_visible_gpos;

        std::string visible_qseq;
        if (edge_to_qstart < 0) {
            visible_qseq = htsacc::seq(p1).substr(-edge_to_qstart);
        } else {
            visible_qseq = htsacc::seq(p1);
        }

        PLOGD << edge_to_qstart;  // TODO log more
        PLOGD << visible_qseq;

        extb::write_string_within (
            extb::to_global (
                query_box,
                {
                    static_cast<int> (i),
                    static_cast<int> ((edge_to_qstart < 0) ? 0 : edge_to_qstart)}
            ),
            query_box,
            visible_qseq
        );

        // extracting and formatting properties to string
        // for tabular display
        for (size_t x = 0; x < prop_callbacks.size(); ++x) {
            prop_cols[x].push_back(prop_callbacks[x](p1));
        }
    }

    PLOGD << "drawing property table";
    table::draw_table(data_box, prop_cols, prop_headers);

    extb::add_attr (
        extb::to_global (query_box, {pui.row_sel, 0}),
        TB_REVERSE);

    extb::add_attr_box(
        make_col (
            ispan (query_box),
            static_cast<int> (seq_browser_j_center)
        ),
        TB_REVERSE
    );

}

void init_pileup_display () {
    auto& pctx = ctx::get<PileupContext>();
    auto& pconf = pctx.config;
    auto& pui = pctx.ui;
    auto& main_pane = ctx::get<GlobalContext>().ui.view;
    extb::clear_box(main_pane);

    const auto mispan = ispan (main_pane);
    const auto mjspan = jspan (main_pane);
    const auto miwidth = mispan.size();
    const auto mjwidth = mjspan.size();

    const auto midsplit = mjspan.first + static_cast<int> (ceil (mjwidth * pconf.query_box_frac));

    const extb::GlobalSpan span_query_j {mjspan.first, midsplit - 1};
    const extb::GlobalSpan span_data_j {midsplit + 1, mjspan.last};

    pui.data_box = make_box ({mispan.first, mispan.last - 2}, span_data_j);

    pui.ref_line = make_row (mispan.first, span_query_j);
    // set ref separator
    extb::set_box (make_row (mispan.first + 1, span_query_j), 0x2500, TB_DIM);
    // set vertical separator
    extb::set_box (make_col (mispan, midsplit), 0x2502);

    pui.query_box = make_box ({mispan.first + 2, mispan.last - 2}, span_query_j);
    // set status separator
    extb::set_box (make_row (mispan.last - 1, mjspan), 0x2500, TB_DIM);
    pui.status_line = make_row (mispan.last, mjspan);  // show e.g. coordinates
}


bool nav_global (tb_event& ev) {
    assert (ev.key);

    auto& gui = ctx::get<GlobalContext>().ui;

    switch (ev.key) {
        case TB_KEY_ENTER:
        extb::clear_box(gui.cmd);
        extb::clear_box(gui.status);
        extb::write_string_within (
            to_global (gui.status, {0, 0}),
            gui.status,
            cmd::exec_cmd(gui.cmd_buf.text).msg,
            TB_DIM
        );
        input::clear(gui.cmd_buf);
        draw_sequence_data();  // NOTE: this is now modal, not global... (future BUG)
        // draw_seq
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


int nav_browser (tb_event& ev) {
    assert (ev.key);

    auto& pc = ctx::get<PileupContext>();
    auto& pui = pc.ui;
    auto& query_box = pui.query_box;

    switch (ev.key) {
        // N.B. selection not really important
        // for mvp - only scrolling of queries really needed
        // (might use > instead, or just no selector for now)
        case TB_KEY_ARROW_DOWN:
        rm_attr (to_global (query_box, {pui.row_sel, 0}), TB_REVERSE);
        ++pui.row_sel;
        pui.row_sel = std::clamp (pui.row_sel, 0, last_local (query_box).i);
        add_attr (to_global (query_box, {pui.row_sel, 0}), TB_REVERSE);
        break;

        case TB_KEY_ARROW_UP:
        rm_attr (to_global (query_box, {pui.row_sel, 0}), TB_REVERSE);
        --pui.row_sel;
        pui.row_sel = std::clamp (pui.row_sel, 0, last_local (query_box).i);
        add_attr (to_global (query_box, {pui.row_sel, 0}), TB_REVERSE);
        break;

        default:
        return false;
    }
    return true;
}


void draw_global_borders () {
    auto& gui = ctx::get<GlobalContext>().ui;


//     // draw fixed elements (carets, borders)
//     // main display
//     // top corners
//     extb::set ({main_box.ispan().first, main_box.jspan().first}, 0x256D);
//     extb::set ({main_box.ispan().first, main_box.jspan().last}, 0x256E);

//     // sides
//     for (auto i = main_box.ispan().first + 1; i <= main_box.ispan().last; ++i) {
//         extb::set ({i, main_box.jspan().first}, 0x2502);
//         extb::set ({i, main_box.jspan().last}, 0x2502);
//     }

//     // top
//     for (auto j =  main_box.jspan().first + 1; j < main_box.jspan().last; ++j) {
//         extb::set ({main_box.ispan().first, j}, 0x2500);
//     }

//     // cmd display
//     // corners
//     extb::set ({cmd_box.ispan().first, cmd_box.jspan().first}, 0x256D);
//     extb::set ({cmd_box.ispan().first, cmd_box.jspan().last}, 0x256E);
//     extb::set ({cmd_box.ispan().last, cmd_box.jspan().last}, 0x256F);
//     extb::set ({cmd_box.ispan().last, cmd_box.jspan().first}, 0x2570);

//     // sides
//     for (auto i = cmd_box.ispan().first + 1; i < cmd_box.ispan().last; ++i) {
//         extb::set ({i, cmd_box.jspan().first}, 0x2502);
//         extb::set ({i, cmd_box.jspan().last}, 0x2502);
//     }

//     // top, bottom
//     for (auto j = cmd_box.jspan().first + 1; j < cmd_box.jspan().last; ++j) {
//         extb::set ({cmd_box.ispan().first, j}, 0x2500);
//         extb::set ({cmd_box.ispan().last, j}, 0x2500);
//     }

//     // status display
//     extb::set ({status_box.ispan().first, status_box.jspan().first}, 0x256D);
//     extb::set ({status_box.ispan().first, status_box.jspan().last}, 0x256E);
//     extb::set ({status_box.ispan().last, status_box.jspan().last}, 0x256F);
//     extb::set ({status_box.ispan().last, status_box.jspan().first}, 0x2570);

//     // sides
//     for (auto i = status_box.ispan().first + 1; i < status_box.ispan().last; ++i) {
//         extb::set ({i, status_box.jspan().first}, 0x2502);
//         extb::set ({i, status_box.jspan().last}, 0x2502);
//     }

//     // top, bottom
//     for (auto j = status_box.jspan().first + 1; j < status_box.jspan().last; ++j) {
//         extb::set ({status_box.ispan().first, j}, 0x2500);
//         extb::set ({status_box.ispan().last, j}, 0x2500);
//     }
}


// TODO: I want to separate calculation and border drawing
// so I can redraw borders on demand
void init_global_ui () {
    auto& gui = ctx::get<GlobalContext>().ui;

    tb_clear();
    // boxes with closed coordinates

    extb::GlobalSpan screen_ispan {0, tb_height() - 1};
    extb::GlobalSpan screen_jspan {0, tb_width() - 1};

    // vertical sectioning of terminal
    extb::GlobalSpan viewer_ispan {screen_ispan.first, screen_ispan.last - CMD_H - STATUS_H};
    extb::GlobalSpan cmd_ispan {viewer_ispan.last + 1, viewer_ispan.last + CMD_H};
    extb::GlobalSpan status_ispan {cmd_ispan.last + 1, cmd_ispan.last + STATUS_H};
    
    if (
        !valid (screen_ispan) ||
        !valid (screen_jspan) ||
        !valid (viewer_ispan) ||
        !valid (cmd_ispan) ||
        !valid (status_ispan)
    ) {
        throw std::runtime_error ("Invalid screen area, terminal likely too small");
    }
    const auto main_box = extb::make_box (
        viewer_ispan,
        screen_jspan
    );
    const auto cmd_box = extb::make_box (
        cmd_ispan,
        screen_jspan
    );
    const auto status_box = extb::make_box (
        status_ispan,
        screen_jspan
    );


    // cmd input
    gui.cmd = extb::make_row (
        cmd_ispan.first + 1,
        {
            screen_jspan.first + 2,      // skip border, leave space for caret :
            screen_jspan.last - 1        // exclude border
        }
    );
    gui.cmd_caret = extb::GlobalCell{cmd_ispan.first + 1, screen_jspan.first + 1};
    extb::set (gui.cmd_caret, ':', TB_DIM);

    extb::GlobalBox status_row = extb::make_row (
        status_ispan.first + 1,
        {
            screen_jspan.first + 1,
            screen_jspan.last - 1
        }
    );

    // for data display
    extb::GlobalBox display_box = extb::make_box (
        {viewer_ispan.first + 1, viewer_ispan.last},
        {screen_jspan.first + 1, screen_jspan.last - 1}
    );

}



void enter_pileup_mode () {
    auto& gctx = ctx::get<GlobalContext>();
    auto& pctx = ctx::get<PileupContext>();

    if (gctx.conf.demo) {
        pctx.data = demo::make_demo_pileup(301, 100);
    }

    init_pileup_display();
    draw_sequence_data();
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
    write_string (
        to_global (gui.status, {0, 0}),
        "Hello!",
        TB_DIM
    );

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
    auto& gconf = gctx.conf;

    while (gconf.run) {
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
            if (ev.key) {
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
        for (const auto& cb_name  : gconf.debug_request) {
            const auto& it = cmd::DEBUG_CALLBACKS.find(cb_name);
            if (it == cmd::DEBUG_CALLBACKS.end()) {
                continue;
            }
            const auto msg = it->second();  // exec
            const auto ncharw =
                extb::write_string ({0, j}, msg);
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
