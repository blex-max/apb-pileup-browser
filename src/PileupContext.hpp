#pragma once

#include "ctx.hpp"
#include "extb.hpp"
#include "hts/boundary-types.hpp"

struct PileupContext : ctx::Context {
    struct {
        int row_sel = 0;
        PileupDisplayBundle pd;
    } data;
    struct {
        struct {
            extb::Box ref_line;
            extb::Box query_box;
            extb::Box status_line;
        } base_display;
        // extb::Box base_props;  // TODO
        double ui_frac_display = 0.4; // the fraction for the rest is implicit (for now)
    } ui;
};


