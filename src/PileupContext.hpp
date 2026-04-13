#pragma once

#include "hts/accessors.hpp"
#include "ctx.hpp"
#include "extb/extb-box.hpp"
#include "hts/boundary-types.hpp"
#include <list>  // preserves insertion order, allows removal by val
#include <unordered_map>
#include <string>


using StringifyFn = std::string(*)(const bam_pileup1_t*);
static std::unordered_map<std::string_view, StringifyFn> BAM_RENDER_CALLBACKS {
    {"qual", [] (const bam_pileup1_t* p1) -> std::string { return std::to_string(htsacc::base_qual(p1)); }},
    {"flag", [] (const bam_pileup1_t* p1) -> std::string { return std::to_string(htsacc::flag (p1)); }}
};


struct PileupContext : ctx::Context {
    PileupData data;
    struct {
        int row_sel = 0;
        extb::box::GlobalBox ref_line;
        extb::box::GlobalBox query_box;
        extb::box::GlobalBox status_line;
        extb::box::GlobalBox data_box;
    } ui;
    struct {
        std::list<std::string> bam_props_request;  // user reqeuested properties to show from pileup data
        double query_box_frac = 0.4;
    } config;
};


