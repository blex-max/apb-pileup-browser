// This TU provides a transfomation interface between htslib data
// and input datatype defined by apb.
#pragma once

#include <htslib/sam.h>
#include <sqlite3.h>

#include <cassert>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>

#include "apb_db/apb_db_core.hpp"


struct transform_err_t {
  enum code_t : uint8_t { aux_parse_fail };
  code_t code;
  bam1_t* b;  // pointer to offending record
};
// transform htslib pileup records into apb input type
// for insertion into apb database.
// Returns a count of records transformed, indicating
// the number of records transformed from the input span
// and the number of valid records in the output span.
// Will transform until one of the two arrays runs out.
// On failure, returns an error type holding a pointer
// to the offending bam1_t and the reason for failure
// (currently, the only expected failure mode is in
// aux parsing).
// NOTE: each written output element is reset to
// default before being filled.
[[nodiscard]] std::expected<size_t, transform_err_t>
apb_transform_pileup1 (
    std::span<const bam_pileup1_t> plpIn, std::span<apb_row1_t> recordsOut,
    const std::function<const char*(int)>& mtid2name
);


// TODO:
// [[nodiscard]] std::expected<size_t, transform_err_t>
// apb_transform_bam1 (
//     std::span<const bam1_t*> samIn,
//     std::span<apb_input_record1_t> recordsOut,
//     const std::function<const char*(int)>& mtid2name,
//     int64_t locusPos_0ho
// );
