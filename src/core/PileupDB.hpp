#pragma once

#include <htslib/sam.h>
#include <string>

#include "core/err.hpp"
#include "core/sql_types.hpp"
#include "core/hts_types.hpp"


struct PileupDB : public SqliteConn {};

// for use as a buffer during conversion
struct PileupFields {
  char base;
  uint8_t baseQual;
  int32_t qPos;
  int indel;
  bool isDel, isHead, isTail, isRefSkip;
  std::string qName;
  uint16_t flag;
  hts_pos_t start;
  hts_pos_t end;
  uint8_t mapQ;
  std::string cig;
  std::string mtidName;
  hts_pos_t mStart;
  std::string seqBases;
  std::string qualAscii;
  std::string auxJson;
};

[[nodiscard]] VoidOrErr init (PileupDB& db);

// NOTE: may return number of records written
// or similar over void
[[nodiscard]] VoidOrErr pileup_to_db (PileupDB& db, const AlnFile& aln, const PileupPosition& pos);

// convert to database-facing interface type
void fill_fields(PileupFields& pf, const bam_pileup1_t* uo_p1, const char* uo_mTidName);

// Copy the in-memory database out to a file on disk, using
// sqlite3's online backup API.
[[nodiscard]] VoidOrErr dump_to_disk (const PileupDB& db, const std::string& path);

// TODO: undefined!
[[nodiscard]] VoidOrErr clear (PileupDB& db);

