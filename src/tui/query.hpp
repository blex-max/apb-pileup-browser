#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// User-supplied query components input at command line
struct QueryFragments {
  std::string where;
  std::string orderBy;
};

enum class Conjunctions : uint8_t {
  AND,
  OR,
};
// Without conjunction, replaces where with cond.
// With conjunction, appends `<conjunction> cond` to where, which
// must be non-empty. Operands are parenthesised only where
// AND/OR precedence would change their meaning: for AND, the
// existing clause and cond are each wrapped if they have a
// top-level OR; for OR nothing is wrapped. So:
// `where flag = [x] OR flag = [y]`, then `and mapq > 0 OR basequal > 35`
// gives `(flag = [x] OR flag = [y]) AND (mapq > 0 OR basequal > 35)`.
// An existing clause containing `--` is always wrapped, so a
// comment errors rather than swallowing cond.
// Sanitises cond before use. No-op if cond is empty.
// Does not check if resulting string is valid sql.
void update_where (
    QueryFragments& query, std::string_view cond,
    std::optional<Conjunctions> conjunction = std::nullopt
);
// Sanitises order before use. No-op if order is empty.
// Does not check if resulting string is valid sql.
void update_order (QueryFragments& query, std::string_view order);

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

  std::string to_string() const;
};
// Get locus metadata from db. Asserts internally; only call once
// locus metadata is known to exist.
PileupMetadata get_locus_data (sqlite3* conn);
