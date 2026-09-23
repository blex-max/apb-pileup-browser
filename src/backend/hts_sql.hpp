#pragma once

#include <htslib/sam.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "backend/hts_types.hpp"
#include "backend/sql_types.hpp"
#include "shared/err.hpp"

struct PileupDB : public SqliteConn {};
VoidOrErr init_db (PileupDB& db);

namespace query {
// TODO: review error handling in this namespace

struct DynamicSelectReadsStmt : public SqliteStmt {
  static inline const std::string_view sqlStmtPrefix =
      "SELECT * FROM reads";
};
struct DynamicFragments {
  std::vector<std::string> where;
  std::string orderBy;
};

using SelectStmtOrErr =
    std::expected<DynamicSelectReadsStmt, Err>;
SelectStmtOrErr prepare_select_reads (
    const PileupDB& db, const DynamicFragments& frags
);

// Steps statement forward by one row. Returns true if a row is available.
[[nodiscard]] BoolOrErr next_read (
    sqlite3_stmt* stmt, const PileupDB& db
);

struct DynamicCountReadsStmt : public SqliteStmt {
  static inline const std::string_view sqlStmtPrefix =
      "SELECT COUNT(*) FROM reads";
};

using CountStmtOrErr = std::expected<DynamicCountReadsStmt, Err>;
CountStmtOrErr prepare_count_reads (
    const PileupDB& db, const std::vector<std::string>& where
);

// locus metadata as extracted from db.
struct PileupMetadata {
  std::string contig;
  int64_t pos;  // 0-based pileup position, per loci.pos
  int64_t start;
  int64_t end;
  std::optional<std::string> refSlice;

  bool valid() const noexcept
  {
    return !contig.empty() && start >= 0 && start < end &&
           pos >= start && pos <= end;
  }
};
// get locus data from pileup db.
std::expected<PileupMetadata, Err> get_locus_data (
    const PileupDB& db
);

// Copy the in-memory database out to a file on disk, using
// sqlite3's online backup API.
[[nodiscard]] VoidOrErr dump_to_disk (
    const PileupDB& db, std::string_view path
);

// Serialize the in-memory database and write the raw bytes to
// stdout, for `--dump -`.
[[nodiscard]] VoidOrErr dump_to_stdout (const PileupDB& db);

// Copy a database file on disk into an in-memory PileupDB,
// using sqlite3's online backup API.
[[nodiscard]] VoidOrErr load_from_disk (
    PileupDB& db, std::string_view path
);

}  // namespace query

namespace hts2sql {

struct InsertPileupErr {
  enum Code : uint8_t { sqlFail, auxParseFail, refFetchFail };

  Code code;
  std::optional<int> sqlRc = std::nullopt;
  std::optional<int> htsRc = std::nullopt;
};
// insert reads at pileup position into database
[[nodiscard]] std::expected<void, InsertPileupErr>
insert_pileup (
    PileupDB& db, const PileupIterator& pileupIter,
    const std::string& contigName, const sam_hdr_t* br_alnHdr,
    const std::optional<FastaFile>& ff
);

// insert pileup locus info into single-row metadata table.
// Returns void or int sqlite3 error code
[[nodiscard]] int insert_metadata (
    PileupDB& db, const std::string& contigName,
    int64_t pileupPos, const GenomicSpan& pileupSpan,
    const std::optional<std::string>& refSlice
);

// Prepare an "INSERT INTO reads (...) VALUES (...)" statement, for use
// with bind_pileup_fields below.
// Returns statment or int sqlite3 error code
std::expected<SqliteStmt, int> prepare_insert_reads_stmt (
    PileupDB& db
);

// flat layout of htslib data to be entered
// into the database for a single record
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

  std::vector<uint32_t> rawCig;
  size_t nCig;
};

// convert to database-facing interface type
// returns true on success, false on failure
// to parse an aux tag in br_p1->b1
[[nodiscard]] bool fill_fields (
    PileupFields& pf, const bam_pileup1_t* br_p1,
    const char* mTidName
);

// Bind one pileup row's fields into `stmt`, in column order matching
// stmt_str_InsertReads. Returns the sqlite3 result code of the first
// failing bind call, or SQLITE_OK if all columns bound successfully.
[[nodiscard]] int bind_pileup_fields (
    SqliteStmt& stmt, const PileupFields& pf
);

// Render a CIGAR array as text (e.g. "151M").
std::string stringify_cigar (
    const uint32_t* br_cig, size_t nCig
);

// converts single aux tag to a json entry
// returns nullopt on failure to parse aux tag.
std::optional<std::string> aux1_to_json (
    const uint8_t* aux1Start, const uint8_t* aux1End
);

// Escape a raw aux string value for embedding in a JSON string literal.
void append_json_escaped (
    const char* br_data, size_t len, std::string& out
);

}  // namespace hts2sql
