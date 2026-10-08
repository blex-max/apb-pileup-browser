#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "app/helpblocks.hpp"
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

inline constinit query::DynamicFragments userClause;

inline constinit sqlite3_stmt* selectStmt = nullptr;
inline constinit uint32_t nStmtRows = 0;  // rows in current stmt
inline constinit int32_t stmtRowScrollOffset = 0;

inline constinit query::PileupMetadata
    locusInfo;  // cached metadata-table row; queried once at init()

struct SetQueryStatus {
  enum Code : uint8_t { success, prepareFail, countFail };
  Code code;
  std::optional<int> rc = std::nullopt;  // sqlite rc on failure
};
// Replace the active query with `clause`: prepares and counts it, then
// swaps in the new statement (finalising the old), clause and row count,
// and resets the row scroll. On failure the active query is untouched;
// details are available from sqlite3_errmsg (conn).
// defined in g_state.cpp
[[nodiscard]] SetQueryStatus set_query (query::DynamicFragments clause);

inline void shutdown()
{
  sqlite3_finalize (selectStmt);
  sqlite3_close_v2 (conn);
}

}  // namespace db

namespace ui {

// defined in g_state.cpp
extern std::array<ColMetadata, 11> tableCols;

inline constinit bool showOverlay = false;

inline constinit struct {
  bool qual = false;
  bool ins = true;
} drawTrackSwitches;

inline constinit struct {
  bool table = true;
} drawPaneSwitches;

namespace browsr {

inline constinit extb::Box frame;
inline constinit extb::HLine alnPaneRefLine;
inline constinit extb::HLine tablePaneHeaderLine;
inline constinit extb::HLine headerSep;
inline constinit extb::Box alnPaneDataBox;
inline constinit extb::VLine vSep;
inline constinit extb::Box tablePaneDataBox;
inline constinit extb::HLine ambientSep;
inline constinit extb::HLine ambientLine;
inline constinit uint16_t nReadOnscreen = 0;
inline constinit int64_t userPanOffset = 0;

}  // namespace browsr

namespace cmd {

inline constexpr auto widgetHeight = 7;  // inc. borders

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

inline constinit extb::Box frame;
inline constinit extb::Box contentBox;
inline constinit helpblocks::TextBlockRef content = helpblocks::app;
inline constinit int contentLnOffset = 0;

}  // namespace overlay

}  // namespace ui

}  // namespace g_state
