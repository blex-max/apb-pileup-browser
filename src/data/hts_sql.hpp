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

#include "data/hts_types.hpp"


// Initialise db with pileup schema (see schema.hpp).
int apb_init_db (sqlite3* conn);

// verify a sqlite3* object for use from apb.
// For e.g. loading from a dump on disk.
// It is recommended to use PRAGMA quick_check
// before verifying for use with apb.
// NOTE: could take output err fill param
// and return bool...
enum class apb_verify_result_t : uint8_t {
  success,
  appMismatch,
  verMismatch,
  schemaMismatch
};
std::expected<apb_verify_result_t, int> apb_verify_schema (sqlite3* conn);

// Formats an sqlite3 return code into a user-facing error.
// FIXME: is this valid or relevant?
// FIXME: in this TU? I don't think so
std::string apb_describe_sqlite_failure (
    std::string_view what, int rc,
    std::optional<std::string_view> conn_msg = std::nullopt
);

// FIXME:
// The data side interface should expose functionality
// for adding a single alignment at a time to the db, and for
// adding a reference based on the span in the db at present, and callers
// should compose upwards. Note that this will mean updating the db,
// iteratively, which means locus table updates should go via UPDATE
// rather than INSERT.
// FIXME: since we're going composable, should add a "check db filled and valid"
// function so users can easily verify that they've done it right and the
// db is full and interally consistent.
// This can be composed with verify_db.

// insert metadata for a single alignment file into db
// returns id for that alignment file for use when subsequently inserting pileup.
// On failure returns sqlite3 integer status code
[[nodiscard]] std::expected<int, int> apb_insert_aln (
    sqlite3* conn, std::string_view alnPath, bool hasContig, int64_t nPlp
);

// estimate the size of an htslib pileup. Useful for gauging
// expected memory usage of application.
uint64_t apb_pileup_size_bytes (std::span<const bam_pileup1_t> plp);

struct insert_pileup_status_t {
  enum Code : uint8_t { success, sqlFail, auxParseFail };
  Code code;
  std::optional<int> rc = std::nullopt;
};
// insert pileup into db for a single alignment file.
// CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
[[nodiscard]] insert_pileup_status_t apb_insert_pileup (
    sqlite3* conn, std::span<const bam_pileup1_t> plp, sqlite3_int64 alnId,
    const std::function<const char*(int)>& mtid2name
);

// Retrieve maxmimum pileup span from reads table in db.
// Does not check if the result is valid.
// Use the result to fetch a reference slice.
// On failure returns sqlite3 int status code.
std::expected<GenomicSpan, int> apb_get_pileup_span (sqlite3* conn);

// Insert pileup locus info into single-row metadata table.
// This should be called last, when all alignment files and pilupes
// have been inserted.
// Returns sqlite3 int status code.
// CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
[[nodiscard]] int apb_insert_locus (
    sqlite3* conn, const std::string& contigName, int64_t pos,
    GenomicSpan span, std::optional<std::string_view> ref
);
