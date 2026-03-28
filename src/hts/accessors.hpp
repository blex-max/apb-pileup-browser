#pragma once

#include <string>

#include "htslib/sam.h"


namespace htsacc {

hts_pos_t qpos (const bam_pileup1_t* p1);

hts_pos_t gstart (const bam_pileup1_t* p1);

std::string seq (const bam_pileup1_t* p1, size_t qpos=0, size_t n=0);

char pileup_base (const bam_pileup1_t* p1);

// or whatever the return type is
uint8_t base_qual (const bam_pileup1_t* p1);

uint16_t flag (const bam_pileup1_t* p1);

}

