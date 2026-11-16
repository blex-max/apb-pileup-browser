// This TU provides an interface between htslib data
// and the database defined by apb.
// Callers should compose these functions as needed,
// see tui/ingest.cpp::ingest_sam_auto for a complete exapmle.
// Transactions (begin/commit) and bounds (e.g. memory usage)
// to be handled by caller.
#pragma once

#include <htslib/sam.h>
#include <sqlite3.h>

#include <cassert>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>

#include "model/apb_db.hpp"


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
[[nodiscard]] std::expected<size_t, transform_p1_err_t>
apb_transform_pileup1 (
    std::span<const bam_pileup1_t> plpIn,
    std::span<apb_input_record1_t> recordsOut,
    const std::function<const char*(int)>& mtid2name
);


// TODO:
// [[nodiscard]] std::expected<size_t, transform_b1_err_t>
// apb_transform_bam1 (
//     std::span<const bam1_t*> bam1In,
//     std::span<apb_input_record1_t> recordsOut,
//     int64_t locusPos,
//     const std::function<const char*(int)>& mtid2name
// );
