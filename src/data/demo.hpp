#pragma once

#include <htslib/sam.h>

#include <cstdint>
#include <string>
#include <vector>

#include "data/hts_types.hpp"

struct SyntheticPileup {
  std::vector<bam_pileup1_t> p;
  GenomicSpan pspan;
  hts_pos_t ppos;
  std::string ref;

  // destroy bam1 storage.
  // owners must call.
  void destroy();
};
// generates a synthetic single-alignment pileup.
SyntheticPileup generate_demo_data (
    uint16_t regWidth, uint16_t nQuery, hts_pos_t gOffset
);
