#pragma once

#include <fmt/format.h>
#include <htslib/faidx.h>
#include <htslib/sam.h>
#include <sqlite3.h>

#include <climits>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "backend/hts_types.hpp"

namespace setup_db {

// Initialise db with pileup schema (see schema.hpp).
sqlite3* init();

struct LoadErr {
  enum Code : uint8_t {
    openFail,
    copyFail,
    contentCorrupt,
    schemaMismatch
  };
  Code code;
  std::optional<int> rc = std::nullopt;
  std::optional<std::string> msg = std::nullopt;
};
// Copy a database file on disk into an new in-memory apb database,
// using sqlite3's online backup API.
std::expected<sqlite3*, LoadErr> load_from_disk (std::string_view path);

}  // namespace setup_db

namespace query {

std::string build_where_clause (const std::vector<std::string>& fragments);

struct DynamicFragments {
  std::vector<std::string> where;
  std::string orderBy;
};
// Compiles `prefix` with the WHERE/ORDER BY built from `frags` appended.
// returns compiled sql statement object, or sqlite3 integer
// return code on failure
// FIXME: using expected implies that you have to null out
// the container returned... better to take an
// output param.
std::expected<sqlite3_stmt*, int> prepare_select_reads (
    sqlite3* conn, std::string_view prefix, const DynamicFragments& frags
);

// Step `stmt` forward by one row.
// returns status code, or sqlite3 integer return code in on failure.
enum class RowIterStatus : uint8_t { rowAvail, exhausted };
[[nodiscard]] std::expected<RowIterStatus, int> next_read (
    sqlite3_stmt* stmt
);

// Steps `stmt` to exhaustion, counting rows.
// Returns the row count, or the sqlite3 return code of the
// first failing step.
std::expected<uint32_t, int> count_rows (sqlite3_stmt* stmt);

// locus metadata as extracted from db.
// 1-indexed!
struct PileupMetadata {
  std::string contig;
  int64_t pos = -1;  // 1-based pileup position, per loci.pos
  int64_t start = -1;
  int64_t end = -1;
  std::optional<std::string> refSlice;

  // Should be redundant with metadata's schema CHECK constraints
  // (schema.hpp) - kept as a backstop.
  bool valid() const noexcept
  {
    return !contig.empty() && start > 0 && end >= start && pos >= start &&
           pos <= end;
  }
};

// Get locus metadata from db. Asserts internally; only call once
// locus metadata is known to exist (post load_from_disk, or after
// apb's own insert_pileup/insert_demo_data in the same process).
PileupMetadata get_locus_data (sqlite3* conn);

struct DiskDumpStatus {
  enum Code : uint8_t {
    success,
    fail,
  };
  Code code;
  std::optional<int> sqlRc = std::nullopt;
  std::optional<std::string> dumpDbMsg = std::nullopt;
};
// Copy the in-memory database out to a file on disk, using
// sqlite3's online backup API.
[[nodiscard]] DiskDumpStatus dump_to_disk (
    sqlite3* conn, std::string_view path
);

// Formats an sqlite3 return code into a user-facing error.
std::string describe_sqlite_failure (
    int rc, std::string_view context,
    std::optional<std::string_view> dbMsg = std::nullopt
);

}  // namespace query

template <>
struct fmt::formatter<query::PileupMetadata>
    : fmt::formatter<std::string> {
  auto format (const query::PileupMetadata& m, format_context& ctx) const
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

namespace hts2sql {

// flat layout of htslib data to be entered
// into the database for a single read.
struct PileupFields {
  // 0-INDEXED: MATCHES RAW HTSLIB REPRESENTATION.
  // Shifted to the DB's 1-indexed representation at bind time,
  // in bind_pileup_fields (hts_sql.cpp).

  // NOTE: layout as table schema
  std::string qName = "?";
  uint16_t flag = UINT16_MAX;
  hts_pos_t start = -1;
  hts_pos_t end = -1;
  uint8_t mapQ = UINT8_MAX;

  std::optional<char> base;
  std::optional<uint8_t> baseQual = UINT8_MAX;
  int32_t qPos = -1;
  int indel = INT_MAX;
  bool isDel = false, isHead = false, isTail = false, isRefSkip = false;

  std::string cig;
  std::string seqBases;
  std::string qualAscii;

  std::string mtidName;
  hts_pos_t mStart = -1;

  std::string auxJson;

  std::vector<uint32_t> rawCig;
  size_t nCig = SIZE_MAX;

  bool valid() const noexcept
  {
    return start >= 0 && end > start && qPos >= 0 && !seqBases.empty() &&
           seqBases.size() == qualAscii.size() && nCig == rawCig.size() &&
           !rawCig.empty();
  }
};

struct InsertPileupStatus {
  enum Code : uint8_t { success, sqlFail, auxParseFail };

  Code code;
  std::optional<int> rc = std::nullopt;
};
// insert reads at pileup position into database.
// CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
// FIXME: status return rather than expected, void.
[[nodiscard]] InsertPileupStatus insert_pileup (
    sqlite3* conn, const bam_pileup1_t* br_plpArr, const size_t nPlp,
    const std::string& contigName, sqlite3_int64 aln_id,
    const Tid2StrFn& mtid2name
);

// insert pileup locus info into single-row metadata table.
// Returns void or int sqlite3 error code
// CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
// FIXME: what error space can this actually return?
[[nodiscard]] int insert_metadata (
    sqlite3* conn, const std::string& contigName, int64_t pileupPos,
    const GenomicSpan& pileupSpan,
    const std::optional<std::string>& refSlice
);

// Prepare an "INSERT INTO reads (...) VALUES (...)" statement, for use
// with bind_pileup_fields.
// exposed for demo.cpp (FIXME)
sqlite3_stmt* prepare_insert_reads_stmt (sqlite3* conn);

// Bind one pileup row's fields into `stmt`, in column order matching
// stmt_str_InsertReads.
// exposed for demo.cpp
void bind_read_data (
    sqlite3_stmt* stmt, const PileupFields& pf, sqlite3_int64 aln_id
);

// Render a CIGAR array as text (e.g. "151M").
// exposed for demo.cpp
std::string stringify_cigar (const uint32_t* br_cig, size_t nCig);

}  // namespace hts2sql

template <>
struct fmt::formatter<hts2sql::PileupFields>
    : fmt::formatter<std::string> {
  auto format (const hts2sql::PileupFields& pf, format_context& ctx) const
  {
    return fmt::formatter<std::string>::format (
        fmt::format (
            "PileupFields{{start: {}, end: {}, qPos: {}, seqBases.size: "
            "{}, qualAscii.size: {}, nCig: {}, rawCig.size: {}}}",
            pf.start, pf.end, pf.qPos, pf.seqBases.size(),
            pf.qualAscii.size(), pf.nCig, pf.rawCig.size()
        ),
        ctx
    );
  }
};
