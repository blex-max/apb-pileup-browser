#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// User-supplied query components input at command line
struct QueryFragments {
  std::string where;
  std::string orderBy;
};

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

namespace tui_state::db {

// ALL MANAGED BY set_query()
inline constinit sqlite3* conn = nullptr;
inline constinit QueryFragments userQuery;
inline constinit std::vector<QueryFragments> queryUndoStack;
inline constinit sqlite3_stmt* selectStmt = nullptr;
inline constinit uint64_t nStmtRows = 0;  // rows in current stmt
inline constinit int32_t stmtRowScrollOffset = 0;
inline constinit uint64_t totalRows = 0;

// Files (multi-aln only) with no reads in the current query, and why.
// Displayed after the reads; counted as scrollable rows.
enum class EmptyFileReason : uint8_t {
  contigAbsent,
  noReadsAtLocus,
  noMatchingReads
};
struct EmptyFile {
  std::string path;
  EmptyFileReason reason;
};
inline constinit std::vector<EmptyFile> queryEmptyFiles;

// Replace the active query with `clause`.
// On failure extra details are available from sqlite3_errmsg (conn).
// Returns sqlite3 integer code
[[nodiscard]] int set_query (const QueryFragments& newQuery);

// total scrollable rows: reads, then empty-file labels
inline uint64_t nScrollRows()
{
  return nStmtRows + static_cast<uint64_t> (queryEmptyFiles.size());
}

inline constinit PileupMetadata
    locusInfo;  // cached metadata-table row; queried once at init()

}  // namespace tui_state::db
