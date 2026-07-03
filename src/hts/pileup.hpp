#pragma once

#include <htslib/sam.h>
#include <htslib/hts.h>
#include <vector>

#include "hts/types.hpp"


struct GenomeSpan {
  hts_pos_t start;
  hts_pos_t end;
};
struct PileupPosition {
  int32_t tid;
  hts_pos_t pos;
};
struct PileupMetadata {
  int32_t tid;
  hts_pos_t pos;
  GenomeSpan span;
};


// non-owning view into const(!) bam_plp_t
// for sorting, querying, etc.
using PileupColumn = std::vector<const bam_pileup1_t*>;

struct PileupBundle {
    PileupMetadata meta {};
    bam_plp_t      storage = nullptr;
    PileupColumn   data;

    PileupBundle() = default;
    ~PileupBundle() noexcept { if (storage) bam_plp_destroy(storage); }
    // NOTE: are these AI move/assignment
    // operator stuff necessary, or is it overkill
    PileupBundle(const PileupBundle&)            = delete;
    PileupBundle& operator=(const PileupBundle&) = delete;
    PileupBundle(PileupBundle&&) noexcept;
    PileupBundle& operator=(PileupBundle&&) noexcept;

};

// NOTE: why isn't this just
// a constructor
void load_pileup (
    PileupBundle& b,
    const htsacc::AlnFile& fh,
    const PileupPosition& pos
);


