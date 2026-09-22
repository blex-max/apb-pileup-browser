#pragma once

#include <htslib/sam.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "backend/PileupDB.hpp"
#include "backend/hts_types.hpp"
#include "backend/sql_types.hpp"
#include "shared/err.hpp"

// -- hts to sql adapter --- //


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

  std::vector<uint32_t> rawCig;
  size_t nCig;
};

/* htslib PILEUP MACHINERY */
struct PileupCapture {
  htsFile* br_fh = nullptr;  // borrowed
  hts_itr_t* o_it = nullptr;
};
struct PreparedPileup {
  PileupCapture* o_cap = nullptr;
  bam_plp_t o_plp = nullptr;
  const bam_pileup1_t* br_plpArr = nullptr;
  size_t nPlp = 0;

  ~PreparedPileup()
  {
    if (o_cap != nullptr) {
      hts_itr_destroy (o_cap->o_it);
      delete o_cap;
    }
    if (o_plp != nullptr) {
      bam_plp_destroy (o_plp);
    }
    br_plpArr = nullptr;
  }
  PreparedPileup() = default;
  PreparedPileup (PreparedPileup&) = delete;
  PreparedPileup& operator= (PreparedPileup&) = delete;
  PreparedPileup (PreparedPileup&& o) noexcept
      : o_cap (o.o_cap),
        o_plp (o.o_plp),
        br_plpArr (o.br_plpArr),
        nPlp (o.nPlp)
  {
    o.o_cap = nullptr;
    o.o_plp = nullptr;
    o.br_plpArr = nullptr;
    o.nPlp = 0;
  };
  PreparedPileup& operator= (PreparedPileup&&) = delete;
};

using PileupOrErr = std::expected<PreparedPileup, Err>;
PileupOrErr prepare_pileup (
    const AlnFile& aln, const PileupLocus& pos
);

// Span (genomic start/end) covered by every read in a prepared pileup.
GenomicSpan get_pileup_span (const PreparedPileup& plp);

// insert the pileup locus into the database's single metadata row.
// Returns void or int sqlite3 error code
[[nodiscard]] std::expected<void, int> insert_metadata (
    PileupDB& db, const PileupMetadata& locus
);

// Prepare an "INSERT INTO reads (...) VALUES (...)" statement, for use
// with bind_pileup_fields below. Exposed directly (not just used
// internally by insert_reads_internal) because demo.cpp drives its own
// insert loop over synthetic data using the same statement/bind pair.
//
// Returns void or int sqlite3 error code
[[nodiscard]] std::expected<SqliteStmt, int>
prepare_insert_reads_stmt (PileupDB& db);

struct InsertReadsErr {
  enum Code : uint8_t { sqlFail, auxParseFail };

  Code code;
  std::optional<int> sqlRc;
};
// Insert reads covering a pileup position into database.
// br_plpArr/nPlp: the pileup array produced by prepare_pileup.
[[nodiscard]] std::expected<void, InsertReadsErr>
insert_reads_internal (
    PileupDB& db, const bam_pileup1_t* br_plpArr, size_t nPlp,
    const Tid2StrFn& tid2str
);

// Bind one pileup row's fields into `stmt`, in column order matching
// stmt_str_InsertReads. Returns the sqlite3 result code of the first
// failing bind call, or SQLITE_OK if all columns bound successfully.
[[nodiscard]] int bind_pileup_fields (
    SqliteStmt& stmt, const PileupFields& pf
);

// convert to database-facing interface type
bool fill_fields (
    PileupFields& pf, const bam_pileup1_t* br_p1,
    const char* mTidName
);

// Render a CIGAR array as its textual form (e.g. "10S40M5I2D").
std::string stringify_cigar (
    const uint32_t* br_cig, size_t nCig
);

// Escape a raw aux string value for embedding in a JSON string literal.
void append_json_escaped (
    const char* br_data, size_t len, std::string& out
);

// converts single aux tag to a json entry
// returns nullopt on failure to parse aux tag.
[[nodiscard]] std::optional<std::string> aux1_to_json (
    const uint8_t* aux1Start, const uint8_t* aux1End
);

// Begin a transaction on `db`. Pair with commit()/rollback() below.
[[nodiscard]] std::expected<void, int> begin_transaction (
    PileupDB& db
);

// Roll back the current transaction on `db`, having
// encountered an error. If rollback fails,
// appends error information to err
// void rollback_on_err (PileupDB& db, Err& err);

// Commit the current transaction on `db`. On failure, attempts a
// rollback and folds the result into the returned error via
// with_rollback_result.
[[nodiscard]] std::expected<void, int> commit (PileupDB& db);
