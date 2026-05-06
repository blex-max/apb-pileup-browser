#include "app.hpp"

#include <format>
#include <stdexcept>

#include "plog/Log.h"

extern "C" {
    #include "termbox2.h"
}
#include "extb/extb.hpp"

#include "table.hpp"
#include "ctx.hpp"
#include "hts/boundary-types.hpp"
#include "util.hpp"
#include "GlobalContext.hpp"
#include "PileupContext.hpp"
#include "cmd.hpp"
#include "demo.hpp"
#include "hts/accessors.hpp"

namespace e2 = extb;
namespace e2b = extb::box;


namespace {  // forward declarations for use in app namespace fns

void init_tb2 ()
{
    setlocale (LC_ALL, "");

    tb_init();
    tb_set_input_mode (TB_INPUT_ESC); // | TB_INPUT_MOUSE for mouse ev
    tb_clear();
};

void init_global_ui (GlobalContext& gctx);
void enter_state (const GlobalContext& gctx);
void handle_event (GlobalContext& gctx, const tb_event& ev);
void draw_screen (GlobalContext& gctx);

}


namespace app {

void init () {
    PLOGD << "Begin global init";

    init_tb2();

    ctx::init<GlobalContext>();
    auto& gctx = ctx::get<GlobalContext>();

    try {
        init_global_ui (gctx);
    } catch (const std::exception &e) {
        throw make_runtime_error ("Error initialising view: {}", e.what());
    }

    gctx.ui.status.buf = "Hello!";  // welcome msg

    // init modal contexts
    ctx::init<PileupContext>();

    enter_state (gctx);

    draw_screen (gctx);

    PLOGD << "Global init complete";
}


void loop ()
{
    tb_event ev{};

    auto& gctx = ctx::get<GlobalContext>();
    auto& gdata = gctx.data;
    auto& gconf = gctx.conf;

    while (gconf.run) {
        tb_poll_event (&ev);
        handle_event (gctx, ev);
        draw_screen (gctx);

        PLOGD << std::format ("Processed frame {}", gdata.frame);
        ++gdata.frame;
    }
}


void shutdown () {
    tb_shutdown();
}

}  // end namespace app


namespace {

// forward declarations
void draw_global_widgets (const GlobalContext& gctx);
void draw_global_borders (const GlobalContext& gctx);
void draw_debug (const GlobalContext& gctx);
void render_input_line ();
void render_status_line ();
void draw_modal_widgets ();
void calc_global_widgets (GlobalContext& gctx);
void draw_pileup (PileupContext& pctx);
void draw_sequence_data (const PileupContext& pctx);
void calc_pileup_layout (PileupContext& pctx);
void draw_pileup_layout (const PileupContext& pctx);
void handle_resize (GlobalContext& gctx);
bool nav_browser (PileupContext& pctx, const tb_event& ev);
bool nav_global (GlobalContext& gctx, const tb_event& ev);
// end forward declarations


/*--- EVENT HANDLING ---*/

void handle_event (GlobalContext& gctx, const tb_event& ev)
{
    auto& gdata = gctx.data;
    auto& gui = gctx.ui;
    
    if (ev.type == TB_EVENT_RESIZE) {
        try {
            handle_resize (gctx);
        }
        catch (const std::exception &e) {
            throw make_runtime_error (
                "Error while attempting to resize view: {}", e.what()
            );
        }
    }

    switch (gdata.state) {
        case (app_state::pileup):
            // render_pileup (ctx);  // TODO
            if (ev.key) {
                // fallthrough until true with short circuit
                // NOTE: this may be fine - an alternative:
                // bool handled = nav_1() || nav_2()...
                // if (!handled) { nav global }
                nav_browser (ctx::get<PileupContext>(), ev)
                || nav_global (gctx, ev);
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
        input::insert (gui.cmd.buf, ev.ch);
    }
}

bool nav_global (GlobalContext& gctx, const tb_event& ev) {
    assert (ev.key);

    auto& gui = gctx.ui;
    auto& cmd_wgt = gui.cmd;
    auto& status_wgt = gui.status;
    std::string ret_msg;

    switch (ev.key) {
        case TB_KEY_ENTER:
            // execute user command
            // NOTE: user commands pull context objects
            // directly as it is difficult to anticipate
            // their needs
            status_wgt.buf = cmd::exec_cmd (cmd_wgt.buf.text).msg;  // return msg
            input::clear (cmd_wgt.buf);
            break;

        case TB_KEY_BACKSPACE:
        case TB_KEY_BACKSPACE2:
            input::del_back(cmd_wgt.buf);
        break;

        default:
        return false;
    }
    return true;

}


bool nav_browser (PileupContext& pctx, const tb_event& ev) {
    assert (ev.key);

    auto& pui = pctx.ui;
    const auto& query_box = pui.query_box;

    switch (ev.key) {
        // N.B. selection not really important
        // for mvp - only scrolling of queries really needed
        // (might use > instead, or just no selector for now)
        case TB_KEY_ARROW_DOWN:
            ++pui.row_sel;
            pui.row_sel = std::clamp (pui.row_sel, 0, e2b::last_local (query_box).i);
            break;

        case TB_KEY_ARROW_UP:
            --pui.row_sel;
            pui.row_sel = std::clamp (pui.row_sel, 0, e2b::last_local (query_box).i);
            break;

        default:
            return false;
    }
    return true;
}


/*--- DRAWING --- */

void handle_resize (GlobalContext& gctx)
{
    calc_global_widgets (gctx);
    switch (gctx.data.state) {
        case app_state::pileup:
            calc_pileup_layout (ctx::get<PileupContext>());
            break;
        default:
            break;
    }
}

void draw_screen (GlobalContext& gctx)
{
    tb_clear();

    /* draw frame */
    draw_global_widgets (gctx);
    draw_modal_widgets ();

    tb_present();
}

void draw_global_widgets (const GlobalContext& gctx)
{
    draw_global_borders (gctx);  // since drawing is now localised
                            // to one fn, maybe each component
                            // can render its own borders?
    render_input_line();
    render_status_line();
    draw_debug (gctx);
    // TODO
}

void draw_debug (const GlobalContext& gctx)
{
    auto& gconf = gctx.conf;

    int j = 0;
    for (const auto& cb_name  : gconf.debug_request) {
        const auto& it = cmd::DEBUG_CALLBACKS.find(cb_name);
        if (it == cmd::DEBUG_CALLBACKS.end()) {
            continue;
        }
        const auto msg = it->second();  // exec
        const auto ncharw =
            e2::write_string ({0, j}, 0, msg);
        if (ncharw < msg.size()) {
            break;
        }
        j += ncharw;
    }
}

void draw_modal_widgets ()
{
    const auto& state = ctx::get<GlobalContext>().data.state;
    // TODO state_initialised flag

    // NOTE only one mode so far, future-proofing
    switch (state) {
        case app_state::pileup:
            draw_pileup (ctx::get<PileupContext>());
            break;

        default:
            return;
    }
}

void draw_pileup (PileupContext& pctx)
{
    draw_pileup_layout (pctx);
    draw_sequence_data (pctx);
}

void render_input_line () {
    const auto& cui = ctx::get<GlobalContext>().ui.cmd;
    const auto& cmd_line = cui.display_line;
    const auto& cmd_buf = cui.buf;
    e2::clear (cmd_line);
    e2::write_string (
        {cmd_line.ispan.first, cmd_line.jspan.first},
        cmd_line.jspan.last,
        cmd_buf.text
    );
}

void render_status_line ()
{
    const auto& status = ctx::get<GlobalContext>().ui.status;
    e2::write_string (
        extb::GlobalCell {
            status.display_line.ispan.first,
            status.display_line.jspan.first
        },
        status.display_line.jspan.last,
        status.buf,
        TB_DIM
    );
}

// TODO/NOTE we do not want to
// perform all this work unless
// it is necessary. So we need a dirty flag
void draw_sequence_data (const PileupContext& pctx) {
    PLOGD << "Begin draw routine for sequence data";

    const auto& pconf = pctx.config;
    const auto& pdat = pctx.data;
    const auto& pui = pctx.ui;
    const auto& data_box = pui.data_box;
    const auto& query_box = pui.query_box;
    const auto& ref_line = pui.ref_line;

    /* setup for extracting user requested display fields for tabular display */

    const auto nprop = pconf.bam_props_request.size();

    std::vector<std::vector<std::string>> prop_cols;  // TODO flat vector with stride rather than nested vectors
    std::vector<std::string> prop_headers;
    std::vector<StringifyFn> prop_callbacks;

    PLOGD << "nprop: " << nprop;
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

    const auto query_box_jspan = query_box.jspan;
    size_t query_box_w = query_box_jspan.size();
    auto seq_browser_local_j_center = (query_box_w / 2);
    auto pileup_gpos = pdat.ps.pos;
    auto pileup_gstart = pdat.ps.gstart;
    int leftmost_visible_gpos = pileup_gpos - seq_browser_local_j_center;
    auto seq_browser_j_center = query_box_jspan.first + seq_browser_local_j_center;

    PLOGD << "drawing ref";
    // NOTE: genomic_substr may fall down later
    // with indels and such. Could make it cigar aware!
    const auto visible_ref_seq =
        genomic_substr(pileup_gstart, leftmost_visible_gpos, query_box_w, pdat.ref.s);
    e2::write_string (
        e2::GlobalCell{ref_line.ispan.first, ref_line.jspan.first},
        ref_line.jspan.last,
        visible_ref_seq
    );

    PLOGD << "drawing queries";
    const auto p1arr = pdat.query.begin.get();
    const auto np1 = pdat.query.n;
    for (size_t i = 0; i < np1; ++i) {
        if ((query_box.ispan.first + i) > query_box.ispan.last) {
            break;
        }
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

        e2::write_string (
            e2::GlobalCell {
                static_cast<int> (query_box.ispan.first + i),
                query_box.jspan.first + static_cast<int> ((edge_to_qstart < 0) ? 0 : edge_to_qstart)
            },
            query_box.jspan.last,
            visible_qseq
        );

        // extracting and formatting properties to string
        // for tabular display
        PLOGD << "prop cols size: " << prop_cols.size();
        for (size_t x = 0; x < prop_callbacks.size(); ++x) {
            prop_cols[x].push_back(prop_callbacks[x](p1));
        }
    }

    PLOGD << "drawing property table";
    table::draw_table (data_box, prop_cols, prop_headers);

    e2::add_attr (
        extb::GlobalCell
            {query_box.ispan.first + pui.row_sel, query_box.jspan.first},
        TB_REVERSE);

    e2::add_attr (
        e2b::make_col (
            query_box.ispan,
            static_cast<int> (seq_browser_j_center)
        ),
        TB_REVERSE);

}

void calc_pileup_layout (PileupContext& pctx)
{
    auto& pconf = pctx.config;
    auto& pui = pctx.ui;
    const auto& viewport = ctx::get<GlobalContext>().ui.main.viewport;

    const auto mispan = viewport.ispan;
    const auto mjspan = viewport.jspan;
    const auto mjwidth = mjspan.size();

    const auto midsplit = mjspan.first + static_cast<int> (ceil (mjwidth * pconf.query_box_frac));

    const e2b::GlobalSpan span_query_j {mjspan.first, midsplit - 1};
    const e2b::GlobalSpan span_data_j {midsplit + 1, mjspan.last};

    pui.data_box = e2b::make_box ({mispan.first, mispan.last - 2}, span_data_j);
    pui.ref_line = e2b::make_row (mispan.first, span_query_j);
    pui.query_box = e2b::make_box ({mispan.first + 2, mispan.last - 2}, span_query_j);
    pui.status_line = e2b::make_row (mispan.last, mjspan);
}

void draw_pileup_layout (const PileupContext& pctx)
{
    const auto& pui = pctx.ui;
    const auto& viewport = ctx::get<GlobalContext>().ui.main.viewport;

    const auto mispan = viewport.ispan;
    const auto mjspan = viewport.jspan;
    const int midsplit = pui.data_box.jspan.first - 1;  // inverse of: span_data_j.first = midsplit + 1

    e2::set (e2b::make_row (mispan.first + 1, pui.query_box.jspan), 0x2500, TB_DIM);
    e2::set (e2b::make_col ({mispan.first, mispan.last - 1}, midsplit), 0x2502);
    e2::set (e2b::make_row (mispan.last - 1, mjspan), 0x2500, TB_DIM);
}

void draw_global_borders (const GlobalContext& gctx) {
    PLOGD << "begin draw routine for global borders";

    auto& gui = gctx.ui;

    // draw fixed elements (carets, borders)
    // main display
    // top corners
    const auto& main_frame = gui.main.frame;
    e2::set (e2b::top_left (main_frame), 0x256D);
    e2::set (e2b::top_right (main_frame), 0x256E);

    // sides
    for (auto i = main_frame.ispan.first + 1; i <= main_frame.ispan.last; ++i) {
        e2::set (e2::GlobalCell {i, main_frame.jspan.first}, 0x2502);
        e2::set (e2::GlobalCell {i, main_frame.jspan.last}, 0x2502);
    }

    // top
    for (auto j =  main_frame.jspan.first + 1; j < main_frame.jspan.last; ++j) {
        e2::set (e2::GlobalCell {main_frame.ispan.first, j}, 0x2500);
    }

    // cmd display
    e2::set (gui.cmd.caret, ':', TB_DIM);
    // corners
    const auto& cmd_frame = gui.cmd.frame;
    e2::set (e2b::top_left (cmd_frame), 0x256D);
    e2::set (e2b::top_right (cmd_frame), 0x256E);
    e2::set (e2b::bottom_left (cmd_frame), 0x251C);
    e2::set (e2b::bottom_right (cmd_frame), 0x2524);

    // sides
    for (auto i = cmd_frame.ispan.first + 1; i < cmd_frame.ispan.last; ++i) {
        e2::set (e2::GlobalCell {i, cmd_frame.jspan.first}, 0x2502);
        e2::set (e2::GlobalCell {i, cmd_frame.jspan.last}, 0x2502);
    }

    // top, bottom
    for (auto j = cmd_frame.jspan.first + 1; j < cmd_frame.jspan.last; ++j) {
        e2::set (e2::GlobalCell {cmd_frame.ispan.first, j}, 0x2500);
        e2::set (e2::GlobalCell {cmd_frame.ispan.last, j}, 0x2500, TB_DIM);
    }

    const auto& status_frame = gui.status.frame;
    // status display
    e2::set (e2b::bottom_right (status_frame), 0x256F);
    e2::set (e2b::bottom_left (status_frame), 0x2570);

    // sides
    for (auto i = status_frame.ispan.first + 1; i < status_frame.ispan.last; ++i) {
        e2::set (e2::GlobalCell {i, status_frame.jspan.first}, 0x2502);
        e2::set (e2::GlobalCell {i, status_frame.jspan.last}, 0x2502);
    }

    // bottom
    for (auto j = status_frame.jspan.first + 1; j < status_frame.jspan.last; ++j) {
        e2::set (e2::GlobalCell {status_frame.ispan.last, j}, 0x2500);
    }
}


void calc_global_widgets (GlobalContext& gctx) {
    auto& gui = gctx.ui;

    e2b::GlobalSpan screen_ispan {0, tb_height() - 1};
    e2b::GlobalSpan screen_jspan {0, tb_width() - 1};

    // vertical sectioning of terminal
    e2b::GlobalSpan viewer_ispan {screen_ispan.first, screen_ispan.last - CMD_H - STATUS_H + 1};
    // overlapping frames
    e2b::GlobalSpan cmd_ispan {viewer_ispan.last + 1, viewer_ispan.last + CMD_H};
    e2b::GlobalSpan status_ispan {cmd_ispan.last, cmd_ispan.last + STATUS_H - 1};

    PLOGD << "screen i last: " << screen_ispan.last;
    PLOGD << "cmd i first: " << cmd_ispan.first;
    PLOGD << "cmd i last: " << cmd_ispan.last;
    PLOGD << "status i first: " << status_ispan.first;
    PLOGD << "status i last: " << status_ispan.last;
    
    if (
        !e2b::valid (screen_ispan) ||
        !e2b::valid (screen_jspan) ||
        !e2b::valid (viewer_ispan) ||
        !e2b::valid (cmd_ispan) ||
        !e2b::valid (status_ispan)
    ) {
        throw std::runtime_error ("Invalid screen area, terminal likely too small");
    }

    // widgets to calc
    auto& main_wgt = gui.main;
    auto& cmd_wgt = gui.cmd;
    auto& status_wgt = gui.status;

    main_wgt.frame = e2b::make_box (
        viewer_ispan,
        screen_jspan
    );
    cmd_wgt.frame = e2b::make_box (
        cmd_ispan,
        screen_jspan
    );
    status_wgt.frame = e2b::make_box (
        status_ispan,
        screen_jspan
    );

    // cmd input
    cmd_wgt.display_line = e2b::make_row (
        cmd_ispan.first + 1,
        {
            screen_jspan.first + 2,  // skip border, leave space for caret :
            screen_jspan.last - 1    // exclude border
        }
    );
    cmd_wgt.caret = e2::GlobalCell {cmd_ispan.first + 1, screen_jspan.first + 1};

    status_wgt.display_line = e2b::make_row (
        status_ispan.first + 1,
        {
            screen_jspan.first + 1,
            screen_jspan.last - 1
        }
    );

    main_wgt.viewport = e2b::make_box (
        {viewer_ispan.first + 1, viewer_ispan.last},
        {screen_jspan.first + 1, screen_jspan.last - 1}
    );

}

void init_global_ui (GlobalContext& gctx)
{
    calc_global_widgets (gctx);
    draw_global_borders (gctx);
}

void enter_pileup_mode (const GlobalContext& gctx, PileupContext& pctx)
{
    if (gctx.conf.demo) {
        pctx.data = demo::make_demo_pileup(301, 100);
    }

    calc_pileup_layout (pctx);
    draw_sequence_data (pctx);
}

void enter_state (const GlobalContext& gctx)
{
    const auto& state = gctx.data.state;
    switch (state) {
        case app_state::pileup:
            enter_pileup_mode (gctx, ctx::get<PileupContext>());
            break;

        default:
            return;
    }
}

// TODO
// void init_browse_state (Context& ctx);

}
