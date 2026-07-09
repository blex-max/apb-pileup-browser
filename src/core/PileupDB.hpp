#pragma once

#include <expected>
#include <htslib/sam.h>
#include <string>

#include "core/err.hpp"
#include "core/sql.hpp"
#include "core/sql_types.hpp"
#include "core/hts_types.hpp"


struct PileupDB : public SqliteConn {
  friend std::expected<PileupDB, Err> make_db ();
  private: PileupDB()=default;  // factory-only construction
};
using DbOrErr = std::expected<PileupDB, Err>;
DbOrErr make_db (); // factory

using InsertReadsStmt = SqliteStmt<struct InsertReadsTag>;
[[nodiscard]] inline int prepare_insert_reads (PileupDB& db, InsertReadsStmt& out) {
  return sqlite3_prepare_v2 (
      db, rsql_InsertReads.data(), static_cast<int> (rsql_InsertReads.size()),
      &out.o_ptr, NULL);
}

// for use as a buffer during conversion
struct PileupFields {
  // NOTE: layout as table schema
  std::string qName;
  uint16_t flag;
  hts_pos_t start;
  hts_pos_t end;
  uint8_t mapQ;

  char base;
  uint8_t baseQual;
  int32_t qPos;
  int indel;
  bool isDel, isHead, isTail, isRefSkip;

  std::string cig;
  std::string seqBases;
  std::string qualAscii;

  std::string mtidName;
  hts_pos_t mStart;
  
  std::string auxJson;
};

// NOTE: may return number of records written
// or similar over void
[[nodiscard]] VoidOrErr insert_pileup (PileupDB& db, const AlnFile& aln, const PileupPosition& pos);

// convert to database-facing interface type
// NOTE: noexcept?
VoidOrErr fill_fields (PileupFields& pf, const bam_pileup1_t* p1, const char* mTidName);

// returns sql rc directly
[[nodiscard]] int bind_pileup_fields (InsertReadsStmt& stmt, const PileupFields& pf);

// Copy the in-memory database out to a file on disk, using
// sqlite3's online backup API.
[[nodiscard]] VoidOrErr dump_to_disk (const PileupDB& db, const std::string& path);

// TODO: undefined!
[[nodiscard]] VoidOrErr clear (PileupDB& db);

