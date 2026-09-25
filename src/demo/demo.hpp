#pragma once

#include "backend/hts_sql.hpp"

struct DemoDataPack {
  hts_pos_t pileupPos;
  GenomicSpan pileupSpan;
  std::vector<hts2sql::PileupFields> reads;
  std::string refSlice;
};
void generate_demo_data (
    uint16_t regWidth, uint16_t nQuery, hts_pos_t gOffset,
    DemoDataPack& out
);

// Insert synthetically-generated demo data into db.
void insert_demo_data (PileupDB& db, const DemoDataPack& data);
