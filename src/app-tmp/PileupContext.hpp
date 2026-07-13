#pragma once

#include <htslib/hts.h>
#include <htslib/sam.h>

#include <list>  // preserves insertion order, allows removal by val
#include <string>
#include <string_view>

#include "ctx.hpp"
#include "extb/extb-box.hpp"
#include "hts/types.hpp"

using StringifyFn = std::string (*) (const bam_pileup1_t*);
StringifyFn get_pileup_text_callback (std::string_view name);
struct PropRequest {
  std::string name;
  StringifyFn cb;
};

struct PileupSpan {
  hts_pos_t gstart;
  hts_pos_t gend;
};

// non-owning view into const(!) bam_plp_t
// for sorting, querying, etc.
using PileupColumn = std::vector<const bam_pileup1_t*>;
struct PileupBundle {
  PileupBundle() = default;
  ~PileupBundle() noexcept
  {
    if (storage) {
      bam_plp_destroy (storage);
    }
  }
  PileupBundle (const PileupBundle&) = delete;
  PileupBundle& operator= (const PileupBundle&) = delete;
  PileupBundle (PileupBundle&&) noexcept;
  PileupBundle& operator= (PileupBundle&&) noexcept;

  PileupPosition pos{};
  PileupSpan span{};
  std::string ref_seq;
  bam_plp_t storage = nullptr;
  PileupColumn data;
};
void load_pileup (
    PileupBundle& b, const htsacc::AlnFile& fh,
    const PileupPosition& pos
);

struct PileupContext : ctx::Context {
  htsacc::AlnFile aln;
  PileupPosition start_pos{};
  PileupBundle data;
  struct {
    int row_start = 0;
    extb::box::GlobalBox ref_line;
    extb::box::GlobalBox query_box;
    extb::box::GlobalBox status_line;
    extb::box::GlobalBox data_box;
  } ui;
  struct {
    std::list<PropRequest> bam_props_request;
    double query_box_frac = 0.4;
  } config;
};
