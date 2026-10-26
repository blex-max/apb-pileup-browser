#pragma once

#include <fmt/format.h>
#include <htslib/sam.h>
#include <sqlite3.h>

#include <climits>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "data/hts_types.hpp"


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

}  // namespace setup_db

namespace hts2sql {

// flat layout of htslib data to be entered
// into the database for a single read.
struct PileupFields {
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

  bool valid() const noexcept
  {
    return start >= 0 && end > start && qPos >= 0 &&
           (!seq || !rawQual || seq->size() == rawQual->size()) &&
           nCig == rawCig.size() && !rawCig.empty();
  }

  std::string to_string() const
  {
    return fmt::format (
        "PileupFields{{start: {}, end: {}, qPos: {}, "
        "nCig: {}, rawCig.size: {}}}",
        start, end, qPos, nCig, rawCig.size()
    );
  }
};

struct InsertPileupStatus {
  enum Code : uint8_t { success, sqlFail, auxParseFail };

  Code code;
  std::optional<int> rc = std::nullopt;
};
// insert reads at pileup position into database.
// CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
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
    const std::optional<std::string>& refSlice, int16_t n_aln
);

// FIXME: insertion of alignment files and so on needs
// to move into this TU

}  // namespace hts2sql

// FIXME: these asserts will attempt to shutdown TUI
// unconditionally and sometimes unnecessarily.
#define APB_S3_EXPECT(conn, fn, val)                                \
  do {                                                              \
    APB_ASSERT (                                                    \
        const auto rc = fn;                                         \
        rc == (val), rc, sqlite3_errstr (rc), sqlite3_errmsg (conn) \
    );                                                              \
  } while (false)

#define APB_S3_EXPECT_OK(conn, fn)                                      \
  do {                                                                  \
    APB_ASSERT (                                                        \
        const auto rc = fn;                                             \
        rc == SQLITE_OK, rc, sqlite3_errstr (rc), sqlite3_errmsg (conn) \
    );                                                                  \
  } while (false)
