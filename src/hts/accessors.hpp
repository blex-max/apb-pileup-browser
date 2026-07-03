#pragma once

#include <string>

#include "htslib/sam.h"


namespace htsacc {

auto start (const bam_pileup1_t* p1);

auto base (const bam_pileup1_t* p1);

auto base_qual (const bam_pileup1_t* p1);

auto mapq (const bam_pileup1_t* p1);

auto flag (const bam_pileup1_t* p1);

auto mtid (const bam_pileup1_t* p1);

auto mpos (const bam_pileup1_t* p1);

// get entire read sequence, or segment of.
std::string seq (const bam_pileup1_t* p1, size_t qpos=0, size_t n=0);

std::string qual_ascii (const bam_pileup1_t* p1, size_t qpos=0, size_t n=0);

}

