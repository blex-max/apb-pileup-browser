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

// verify that the content of an apb db is well formed:
// schema constraints, foreign keys, and metadata present
// and consistent with records.
struct apb_validate_result_t {
  enum codes_t : uint8_t {
    success,
    failedIntegrityCheck,
    failedForeignKeyCheck,
    missingLocus,
    metadataMismatch  // metadata contradicts records; see problems
  };
  codes_t code;
  std::optional<std::string> problems = std::nullopt;
};
[[nodiscard]] std::expected<apb_validate_result_t, int> apb_validate_db (
    sqlite3* conn
);

// insert a record source, e.g. an alignment file, into db.
// Returns its id, for apb_insert_records.
// On failure returns sqlite3 integer status code
[[nodiscard]] std::expected<int, int> apb_insert_source (
    sqlite3* conn, std::string_view source, bool hasContig
);

struct apb_pileup1_t {
  int32_t qPos_0ho = -1;
  int indel = INT_MAX;
  // bitfield
  uint8_t is_del : 1 = 0, is_refskip : 1 = 0;
};
// htslib data to be entered
// into the database for a single read.
struct apb_row1_t {
  // 0-INDEXED - MATCHES RAW HTSLIB REPRESENTATION.
  // For loading of surrounding reads not aligned
  // to a specific locus, plp is nullopt.

  int64_t start_0ho = -1;
  std::vector<uint32_t> cigBlob;

  std::optional<int64_t> end_0ho = std::nullopt;
  std::optional<std::string> seq = std::nullopt;
  std::optional<std::string> qual = std::nullopt;  // phred+33
  std::optional<uint16_t> flag = std::nullopt;
  std::optional<uint8_t> mapQ = std::nullopt;
  std::optional<std::string> mtidName = std::nullopt;
  std::optional<int64_t> mStart_0ho = std::nullopt;
  std::optional<std::string> qName = std::nullopt;
  std::optional<std::string> auxJson = std::nullopt;
  std::optional<apb_pileup1_t> plp = std::nullopt;
};

// insert records, with or without pileup, for a single source.
// Returns SQLITE_OK on success, sqlite err code on failure.
// CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
[[nodiscard]] int apb_insert_records (
    sqlite3* conn, std::span<const apb_row1_t> recs, sqlite3_int64 sourceId
);

// Insert pileup locus info into single-row metadata table.
// Call last, once all sources and records are inserted:
// the records span and n_sources are derived from those tables.
// ref, if given, must cover exactly that span.
// Returns SQLITE_OK on success, sqlite err code on failure.
// CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
[[nodiscard]] int apb_insert_locus (
    sqlite3* conn, const std::string& contigName, int64_t pos_0ho,
    std::optional<std::string_view> ref
);
