// This TU provides an interface between htslib data
// and the database defined by apb.
// Callers should compose these functions as needed,
// see app/ingest.cpp::ingest_sam_auto for a complete exapmle.
// Transactions (begin/commit) and bounds (e.g. memory usage)
// to be handled by caller.
#pragma once

#include <fmt/format.h>
#include <htslib/faidx.h>
#include <htslib/sam.h>
#include <sqlite3.h>

#include <cassert>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "hts_types.hpp"


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
  // 0-INDEXED: MATCHES RAW HTSLIB REPRESENTATION.

  // NOTE: layout as table schema
  std::string qName = "?";
  uint16_t flag = UINT16_MAX;
  hts_pos_t start = -1;
  hts_pos_t end = -1;
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
  std::optional<hts_pos_t> mStart = std::nullopt;

  std::string auxJson;

  std::vector<uint32_t> rawCig;
  size_t nCig = SIZE_MAX;
};
bool valid (const apb_input_record1_t& rec) noexcept;
std::string to_string (const apb_input_record1_t& rec);

struct transform_p1_err_t {
  enum code_t : uint8_t { aux_parse_fail };
  code_t code;
  bam1_t* b;  // pointer to offending record
};
// transform htslib pileup records into apb input type
// for insertion into apb database. Agnostic as to array-like
// used for input and output. Returns a count of records
// transformed, indicating the number of records transformed
// from the input span and the number of valid records in
// the output span. Will transform until one of the two
// arrays runs out.
// On failure, returns an error type holding a pointer
// to the offending bam1_t and the reason for failure
// (currently, the only expected failure mode is in
// aux parsing).
// NOTE: overwrites elements in apb_input_record1_t
// output elements unconditionally. Does not clear.
// FIXME: must test overwrite path is complete -
// no old data left behind.
// FIXME: this TU, and the input struct especially, shouldn't depend
// on htslib at all. This fn should probably move up down a layer.
// (perhaps ingestion/transformation should move from here+app/ to generic/.
// This TU is just database management. Does that mean load/dump should be in here
// after all (lol)? Think on it.)
[[nodiscard]] std::expected<size_t, transform_p1_err_t>
apb_transform_pileup1 (
    std::span<const bam_pileup1_t> plpIn,
    std::span<apb_input_record1_t> recordsOut,
    const std::function<const char*(int)>& mtid2name
);

// insert pileup into db for a single alignment file.
// Returns SQLITE_OK on success, sqlite err code on failure.
// CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
[[nodiscard]] int apb_insert_pileup (
    sqlite3* conn, std::span<const apb_input_record1_t> recs,
    sqlite3_int64 alnId
);

// Insert pileup locus info into single-row metadata table.
// This should be called last, when all alignment files and pilupes
// have been inserted, as it retrieves data from those tables
// Returns sqlite3 int status code on failure.
// CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
[[nodiscard]] std::expected<void, int> apb_insert_locus (
    sqlite3* conn, const std::string& contigName, int64_t pos,
    genomic_span_t span, std::optional<std::string_view> ref
);
