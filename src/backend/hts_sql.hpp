#pragma once

#include <htslib/sam.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "backend/hts_types.hpp"
#include "backend/sql_types.hpp"
#include "shared/err.hpp"

struct PileupDB {
  sqlite3* o_conn = nullptr;
  operator sqlite3*() const { return o_conn; }

  PileupDB() = default;
  PileupDB (const PileupDB&) = delete;
  PileupDB& operator= (const PileupDB&) = delete;

  PileupDB (PileupDB&& other) noexcept : o_conn (other.o_conn)
  {
    other.o_conn = nullptr;
  }
  PileupDB& operator= (PileupDB&&) = delete;

  ~PileupDB()
  {
    if (o_conn != nullptr) {
      sqlite3_close_v2 (o_conn);
    }
  }

  // Initialise db with pileup schema (see schema.hpp)
  // returns db on success, else sqlite3 error code
  static std::expected<PileupDB, int> init();

  struct LoadStatus {
    enum Code : uint8_t {
      success,
      openFail,
      copyFail,
      verifyError,  // should be unreachable
      schemaMismatch
    };
    Code code;
    std::optional<int> sqlRc = std::nullopt;
    std::optional<std::string> sqlMsg = std::nullopt;
  };
  // Copy a database file on disk into an in-memory PileupDB,
  // using sqlite3's online backup API.
  static LoadStatus load_from_disk (
      PileupDB& db, std::string_view path
  );
};

namespace query {

struct DynamicSelectReadsStmt : public SqliteStmt {
  static inline const std::string_view sqlStmtPrefix =
      "SELECT * FROM reads";

  // could be a member. Oh well, TODO
  struct DynamicFragments {
    std::vector<std::string> where;
    std::string orderBy;
  };

  // returns compiled sql statement object, or sqlite3 integer
  // return code on failure
  static std::expected<DynamicSelectReadsStmt, int>
  prepare_select_reads (
      const PileupDB& db, const DynamicFragments& frags
  );
};

struct DynamicCountReadsStmt : public SqliteStmt {
  static inline const std::string_view sqlStmtPrefix =
      "SELECT COUNT(*) FROM reads";

  // returns compiled sql statement object, or sqlite3 integer
  // return code on failure
  static std::expected<DynamicCountReadsStmt, int>
  prepare_count_reads (
      const PileupDB& db, const std::vector<std::string>& where
  );
};

// Step `stmt` forward by one row.
// returns status code, or sqlite3 integer return code in on failure.
enum class RowIterStatus : uint8_t { rowAvail, exhausted };
[[nodiscard]] std::expected<RowIterStatus, int> next_read (
    sqlite3_stmt* stmt
);

// Steps `stmt` to exhaustion, counting rows.
// Returns the row count, or the sqlite3 return code of the
// first failing step.
std::expected<uint32_t, int> count_rows (sqlite3_stmt* stmt);

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
// Returns PileupMetadata on success, or sqlite3 return code on failure.
std::expected<PileupMetadata, int> get_locus_data (
    const PileupDB& db
);

struct DiskDumpStatus {
  enum Code : uint8_t {
    success,
    fail,
  };
  Code code;
  std::optional<int> sqlRc = std::nullopt;
  std::optional<std::string> dumpDbMsg = std::nullopt;
};
// Copy the in-memory database out to a file on disk, using
// sqlite3's online backup API.
[[nodiscard]] DiskDumpStatus dump_to_disk (
    const PileupDB& db, std::string_view path
);

// Serialize the in-memory database and write the raw bytes to
// stdout, for `--dump -`.
struct StdoutDumpStatus {
  enum Code : uint8_t {
    success,
    sqliteSerialiseFail,
    writeFail,
  };
  Code code;
};
[[nodiscard]] StdoutDumpStatus dump_to_stdout (
    const PileupDB& db
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

struct Aux1ToJsonErr {
  enum Codes : uint8_t {
    parseFail,
  };
};
// converts single aux tag to a json entry
// returns unexpected on failure to parse aux tag.
std::expected<std::string, Aux1ToJsonErr::Codes> aux1_to_json (
    const uint8_t* aux1Start, const uint8_t* aux1End
);

// Escape a raw aux string value for embedding in a JSON string literal.
void append_json_escaped (
    const char* br_data, size_t len, std::string& out
);

}  // namespace hts2sql
