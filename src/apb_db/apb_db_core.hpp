// This TU provides a basic interface for the database defined by apb.
// Callers should compose these functions as needed
// (See tui ingestion logic for an example).
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

struct apb_verify_result_t {
  enum codes_t : uint8_t {
    success,
    versionUnset,
    verMismatch,
    fkNotEnabled,
    schemaMismatch
  };
  codes_t code;
  int version = -1;
};
// verify a db has the expected schema for use with apb.
[[nodiscard]] std::expected<apb_verify_result_t, int> apb_verify_schema (
    sqlite3* conn
);

// verify that the content of an apb db is well formed
struct apb_validate_result_t {
  enum codes_t : uint8_t {
    success,
    failedIntegrityCheck,
    failedForeignKeyCheck,
    missingLocus
  };
  codes_t code;
  std::optional<std::string> problems = std::nullopt;
};
[[nodiscard]] std::expected<apb_validate_result_t, int> apb_validate_db (
    sqlite3* conn
);

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
  int32_t qPos_0ho = -1;
  int indel = INT_MAX;
  bool isDel = false, isHead = false, isTail = false, isRefSkip = false;

  std::string cig;
  std::optional<std::string> seq = std::nullopt;
  std::optional<std::vector<uint8_t>> rawQual = std::nullopt;

  std::optional<std::string> mtidName = std::nullopt;
  std::optional<int64_t> mStart_0ho = std::nullopt;

  std::string auxJson;

  std::vector<uint32_t> rawCig;
};

// insert pileup into db for a single alignment file.
// Returns SQLITE_OK on success, sqlite err code on failure.
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
    sqlite3* conn, const std::string& contigName, int64_t pos_0ho,
    int64_t spanStart_0ho, int64_t spanEnd_0ho,
    std::optional<std::string_view> ref
);
