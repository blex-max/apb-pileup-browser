// This TU provides an interface between htslib data
// and the database defined by apb.
// Callers should compose these functions as needed,
// see tui/ingest.cpp::ingest_sam_auto for a complete exapmle.
// Transactions (begin/commit) and bounds (e.g. memory usage)
// to be handled by caller.
#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>


// Create the apb schema on an open db connection
[[nodiscard]] int apb_create_schema (sqlite3* conn);

enum class apb_verify_result_t : uint8_t {
  success,
  appMismatch,
  verMismatch,
  pragmaMismatch,
  schemaMismatch
};
// verify a sqlite3* object for use from apb.
// For e.g. loading from a dump on disk.
// It is recommended to use PRAGMA quick_check
// before verifying for use with apb.
[[nodiscard]] std::expected<apb_verify_result_t, int> apb_verify_schema (
    sqlite3* conn
);

// FIXME: since we're going composable, should add a "check db filled and valid"
// function so users can easily verify that they've done it right and the
// db is full and interally consistent.
// This can be composed with verify_schema.
// But what would this actually test though? I guess that any alignment which
// has reads has the correct number of reads, that any inserted reference matches
// the pileup span length, and an sqlite3 integrity check. And perhaps that
// ncig matches cig bytes (but can this be a check/trigger in the schema?).
// That all reads cover the locus?
// That locus and alignment tables have some content.
// enum class apb_validate_result_t : uint8_t {
//   success,
//   corrupt,
//   readsMismatch
// };
// [[nodiscard]] std::expected<apb_validate_result_t, int> apb_validate_db (sqlite3* conn);

// insert metadata for a single alignment file into db
// returns id for that alignment file for use when subsequently inserting pileup.
// On failure returns sqlite3 integer status code
[[nodiscard]] std::expected<int, int> apb_insert_aln (
    sqlite3* conn, std::string_view alnPath, bool hasContig
);

// flat layout of htslib data to be entered
// into the database for a single read.
struct apb_input_record1_t {
  // 0-INDEXED - MATCHES RAW HTSLIB REPRESENTATION.
  // FIXME: for loading reads from nearby locus,
  // more fields will need to be optional, and
  // there should be an e.g. aligned_to_locus boolean.

  // NOTE: layout as table schema
  std::string qName = "?";
  uint16_t flag = UINT16_MAX;
  int64_t start_0ho = -1;
  int64_t end_0ho = -1;
  uint8_t mapQ = UINT8_MAX;

  std::optional<char> base = std::nullopt;
  std::optional<uint8_t> baseQual = std::nullopt;
  int32_t qPos = -1;
  int indel = INT_MAX;
  bool isDel = false, isHead = false, isTail = false, isRefSkip = false;

  std::string cig;
  std::optional<std::string> seq = std::nullopt;
  std::optional<std::vector<uint8_t>> rawQual = std::nullopt;

  std::optional<std::string> mtidName = std::nullopt;
  std::optional<int64_t> mStart = std::nullopt;

  std::string auxJson;

  std::vector<uint32_t> rawCig;
  size_t nCig = SIZE_MAX;
};

// insert pileup into db for a single alignment file.
// CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
[[nodiscard]] int apb_insert_records (
    sqlite3* conn, std::span<const apb_input_record1_t> recs,
    sqlite3_int64 alnId
);

// Insert pileup locus info into single-row metadata table.
// This should be called last, when all alignment files and pilupes
// have been inserted, as it retrieves data from those tables
// Returns SQLITE_OK on success, sqlite err code on failure.
// CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
[[nodiscard]] int apb_insert_locus (
    sqlite3* conn, const std::string& contigName, int64_t pos,
    int64_t span0Start, int64_t span0End,
    std::optional<std::string_view> ref
);
