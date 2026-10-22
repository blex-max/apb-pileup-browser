#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "app/helpblocks.hpp"
#include "app/sql.hpp"
#include "backend/hts_sql.hpp"
#include "frontend/extb/box/box.hpp"
#include "frontend/extb/extb.hpp"
#include "frontend/history.hpp"
#include "frontend/input.hpp"

struct ColMetadata {
  bool visible;
  const std::string_view fieldName;
  const uint16_t displayWidth;
  std::string (*const fn_retrieve_from_db) (sqlite3_stmt*);
};

namespace g_state {

inline constinit bool run = true;

namespace db {

inline constinit sqlite3* conn = nullptr;
inline constinit sql::QueryFragments userClause;
inline constinit sqlite3_stmt* selectStmt = nullptr;
inline constinit uint64_t nStmtRows = 0;  // rows in current stmt
inline constinit int32_t stmtRowScrollOffset = 0;

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
inline constinit std::vector<EmptyFile> emptyFiles;

// total scrollable rows: reads, then empty-file labels
inline uint64_t nScrollRows()
{
  return nStmtRows + static_cast<uint64_t> (emptyFiles.size());
}

inline constinit sql::PileupMetadata
    locusInfo;  // cached metadata-table row; queried once at init()

enum class SetQueryStatus : uint8_t { success, guardFail, s3Fail };
// Replace the active query with `clause`.
// On failure extra details are available from sqlite3_errmsg (conn).
[[nodiscard]] SetQueryStatus set_query (sql::QueryFragments userFrags);

inline void shutdown()
{
  sqlite3_finalize (selectStmt);
  sqlite3_close_v2 (conn);
}

}  // namespace db

// NOTE: it may be cleaner to split this into a separate TU,
// especially if there is to be another renderer
namespace ui {

// defined in g_state.cpp
extern std::array<ColMetadata, 10> tableCols;

inline constinit bool showOverlay = false;
// last resize left terminal too small; widget geometry is stale
inline constinit bool tooSmall = false;

inline constinit bool drawQualTrack = false;
inline constinit bool drawInsTrack = true;
inline constinit bool drawTable = true;

inline constexpr auto kMinWidth = 20;

namespace browsr {

// Can't meaningfully display data below this height
inline constexpr auto kMinWidgetHeight = 8;

inline constinit extb::Box frame;
inline constinit extb::HLine alnPaneRefLine;
inline constinit extb::HLine tablePaneHeaderLine;
inline constinit extb::HLine headerSep;
inline constinit extb::Box alnPaneDataBox;
inline constinit extb::VLine vSep;
inline constinit extb::Box tablePaneDataBox;
inline constinit extb::HLine ambientSep;
inline constinit extb::HLine ambientLine;
// scroll units: reads + empty-file labels
inline constinit uint16_t nUnitsOnscreen = 0;
inline constinit int64_t userPanOffset = 0;

}  // namespace browsr

namespace cmd {

// cmd widget has fixed height. browser widget
// scales to accomodate.
inline constexpr auto kWidgetHeight = 7;  // inc. borders

inline constinit extb::Box frame;
inline constinit extb::HLine
    queryStatusLine;  // for displaying current filter applied to records
inline constinit extb::HLine statusSep;
inline constinit extb::HLine inputLine;
inline constinit extb::GlobalCell inputCaret;
inline constinit EditBuf inputBuf;
inline constinit CmdHistory history;
inline constinit extb::HLine sepLine;
inline constinit extb::HLine msgLine;
inline constinit std::string msgBuf;

}  // namespace cmd

namespace overlay {

inline constexpr auto kMinWidgetWidth = 10;
inline constexpr auto kMinWidgetHeight = 10;

inline constinit extb::Box frame;
inline constinit extb::Box contentBox;
inline constinit helpblocks::TextBlockRef content = helpblocks::app;
inline constinit int contentLnOffset = 0;

}  // namespace overlay

}  // namespace ui

}  // namespace g_state


// FIXME: use this idiom
#define APB_S3_EXPECT(fn, val)                                    \
  do {                                                            \
    APB_ASSERT (                                                  \
        const auto rc = fn; rc == (val), rc, sqlite3_errstr (rc), \
                            sqlite3_errmsg (g_state::db::conn)    \
    );                                                            \
  } while (false)

#define APB_S3_EXPECT_OK(fn)                                          \
  do {                                                                \
    APB_ASSERT (                                                      \
        const auto rc = fn; rc == SQLITE_OK, rc, sqlite3_errstr (rc), \
                            sqlite3_errmsg (g_state::db::conn)        \
    );                                                                \
  } while (false)
