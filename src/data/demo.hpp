#pragma once

#include <htslib/sam.h>

#include <cstdint>
#include <string>
#include <vector>

#include "data/hts_types.hpp"

struct synthetic_pileup_t {
  std::vector<bam_pileup1_t> p;
  GenomicSpan pspan;
  hts_pos_t ppos;
  std::string ref;

  // owners must call.
  void destroy()
  {
    for (auto& p1 : p) {
      bam_destroy1 (p1.b);
      p1.b = nullptr;
    }
  }
};
// generates a synthetic single-alignment pileup.
synthetic_pileup_t generate_demo_data (
    uint16_t regWidth, uint16_t nQuery, hts_pos_t pos
);
