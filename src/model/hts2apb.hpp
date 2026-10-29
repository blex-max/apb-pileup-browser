// This TU provides an interface between htslib data
// and the database defined by apb.
// Callers should compose these functions as needed,
// see app/ingest.cpp::ingest_sam_auto for a complete exapmle.
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


// Create the apb schema on a db connection
[[nodiscard]] int apb_create_schema (sqlite3* conn);

// verify a sqlite3* object for use from apb.
// For e.g. loading from a dump on disk.
// It is recommended to use PRAGMA quick_check
// before verifying for use with apb.
enum class apb_verify_result_t : uint8_t {
  success,
  appMismatch,
  verMismatch,
  schemaMismatch
};
std::expected<apb_verify_result_t, int> apb_verify_schema (sqlite3* conn);

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

struct insert_pileup_err_t {
  enum Kind : uint8_t { sqlFail, auxParseFail };
  Kind kind;
  std::optional<int> rc = std::nullopt;
};
// insert pileup into db for a single alignment file.
// CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
[[nodiscard]] std::expected<void, insert_pileup_err_t> apb_insert_pileup (
    sqlite3* conn, std::span<const bam_pileup1_t> plp, sqlite3_int64 alnId,
    const std::function<const char*(int)>& mtid2name
);

// Insert pileup locus info into single-row metadata table.
// This should be called last, when all alignment files and pilupes
// have been inserted.
// Returns sqlite3 int status code.
// CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
// FIXME: since db may be being updated iteratively
// locus table updates should be able to go via UPDATE rather than INSERT.
// Another fn?
[[nodiscard]] int apb_insert_locus (
    sqlite3* conn, const std::string& contigName, int64_t pos,
    genomic_span_t span, std::optional<std::string_view> ref
);
