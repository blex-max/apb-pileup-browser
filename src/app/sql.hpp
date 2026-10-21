#pragma once

#include <fmt/format.h>
#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sql {

// User-supplied pieces of the current query, as typed.
struct QueryFragments {
  std::vector<std::string> where;
  std::string orderBy;
};

// Joins WHERE fragments into a single expression, left-wrapping
// each in parens: later fragments begin with their own connective
// (e.g. "OR b", "AND c"), so {"a", "OR b", "AND c"} gives
// "((a) OR b) AND c". Empty if no fragments. No "WHERE" keyword.
std::string process_where (const std::vector<std::string>& where);

// locus metadata as extracted from db.
// 1-indexed!
struct PileupMetadata {
  std::string contig;
  int64_t pos = -1;  // 1-based pileup position, per loci.pos
  int64_t start = -1;
  int64_t end = -1;
  std::optional<std::string> refSlice;
  int16_t nAln = -1;

  // Should be redundant with metadata's schema CHECK constraints
  // (schema.hpp) - kept as a backstop.
  bool valid() const noexcept
  {
    return !contig.empty() && start > 0 && end >= start && pos >= start &&
           pos <= end && nAln > 0;
  }
};
// Get locus metadata from db. Asserts internally; only call once
// locus metadata is known to exist.
PileupMetadata get_locus_data (sqlite3* conn);

}  // namespace sql

template <>
struct fmt::formatter<sql::PileupMetadata> : fmt::formatter<std::string> {
  auto format (const sql::PileupMetadata& m, format_context& ctx) const
  {
    return fmt::formatter<std::string>::format (
        fmt::format (
            "PileupMetadata{{contig: {}, pos: {}, start: {}, end: {}}}",
            m.contig, m.pos, m.start, m.end
        ),
        ctx
    );
  }
};
