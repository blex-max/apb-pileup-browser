#pragma once

#include "ctx.hpp"
#include "extb.hpp"
#include "hts/boundary-types.hpp"
#include <set>
#include <unordered_map>
#include <string>


using StringifyFn = std::string(*)(QueryRep);
static std::unordered_map<std::string_view, StringifyFn> BAM_RENDER_CALLBACKS {
  {"qual", [] (QueryRep q) -> std::string { return std::to_string(q.qual); }},
  {"flag", [] (QueryRep q) -> std::string { return std::to_string(q.flag); }}
};


struct PileupContext : ctx::Context {
    struct {
        int row_sel = 0;
        PileupDisplayBundle pd;  // not sure this is where data will be held...?
    } data;
    struct {
        extb::Box ref_line;
        extb::Box query_box;
        extb::Box status_line;
        extb::Box data_box;
    } ui;
    struct {
        std::set<std::string> bam_props_request;  // user reqeuested properties to show from pileup data
        double query_box_frac = 0.4;
    } config;
};


