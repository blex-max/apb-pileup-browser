#include "app/g_state.hpp"

#include <fmt/format.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "backend/hts_sql.hpp"
#include "backend/schema.hpp"
#include "shared/apb_assert.hpp"
#include "shared/cleanup.hpp"

namespace g_state::ui {

constinit std::array<ColMetadata, 11> tableCols{{
    {false, "qpos", 6,
     [] (sqlite3_stmt* row) {
       return std::to_string (
           sqlite3_column_int (row, schema::UserReadViewSelect::qpos)
       );
     }},
    {true, "basequal", 12,
     [] (sqlite3_stmt* row) {
       if (sqlite3_column_type (
               row, schema::UserReadViewSelect::basequal
           ) == SQLITE_NULL) {
         return std::string{"*"};
       }
       return std::to_string (
           sqlite3_column_int (row, schema::UserReadViewSelect::basequal)
       );
     }},
    {true, "flag", 6,
     [] (sqlite3_stmt* row) {
       return std::to_string (
           sqlite3_column_int (row, schema::UserReadViewSelect::flag)
       );
     }},
    {true, "mapq", 6,
     [] (sqlite3_stmt* row) {
       return std::to_string (
           sqlite3_column_int (row, schema::UserReadViewSelect::mapq)
       );
     }},
    {false, "start", 14,
     [] (sqlite3_stmt* row) {
       return std::to_string (
           sqlite3_column_int64 (row, schema::UserReadViewSelect::start)
       );
     }},
    {false, "end", 14,
     [] (sqlite3_stmt* row) {
       return std::to_string (
           sqlite3_column_int64 (row, schema::UserReadViewSelect::end)
       );
     }},
    {true, "cigar", 16,
     [] (sqlite3_stmt* row) {
       const auto* p =
           sqlite3_column_text (row, schema::UserReadViewSelect::cigar);
       const auto len =
           sqlite3_column_bytes (row, schema::UserReadViewSelect::cigar);
       return std::string (
           reinterpret_cast<const char*> (p), static_cast<size_t> (len)
       );
     }},
    {false, "qname", 21,
     [] (sqlite3_stmt* row) {
       if (sqlite3_column_type (row, schema::UserReadViewSelect::qname) ==
           SQLITE_NULL) {
         return std::string{"*"};
       }
       const auto* p =
           sqlite3_column_text (row, schema::UserReadViewSelect::qname);
       const auto len =
           sqlite3_column_bytes (row, schema::UserReadViewSelect::qname);
       return std::string (
           reinterpret_cast<const char*> (p), static_cast<size_t> (len)
       );
     }},
    {false, "mtid", 10,
     [] (sqlite3_stmt* row) {
       if (sqlite3_column_type (row, schema::UserReadViewSelect::mtid) ==
           SQLITE_NULL) {
         return std::string{"*"};
       }
       const auto* p =
           sqlite3_column_text (row, schema::UserReadViewSelect::mtid);
       const auto len =
           sqlite3_column_bytes (row, schema::UserReadViewSelect::mtid);
       return std::string (
           reinterpret_cast<const char*> (p), static_cast<size_t> (len)
       );
     }},
    {false, "mstart", 14, [] (sqlite3_stmt* row) {
       if (sqlite3_column_type (row, schema::UserReadViewSelect::mstart) ==
           SQLITE_NULL) {
         return std::string ("*");
       }
       return std::to_string (
           sqlite3_column_int64 (row, schema::UserReadViewSelect::mstart)
       );
     }},
}};

}  // namespace g_state::ui

namespace g_state::db {

SetQueryStatus set_query (query::DynamicFragments clause)
{
  auto prepResult = query::prepare_select_reads (
      conn, schema::UserReadViewSelect::sqlPrefix, clause
  );
  if (!prepResult) {
    return {.code = SetQueryStatus::prepareFail, .rc = prepResult.error()};
  }
  auto* newStmt = *prepResult;
  const auto rowCountResult = query::count_rows (newStmt);
  if (!rowCountResult) {
    sqlite3_finalize (newStmt);
    return {
        .code = SetQueryStatus::countFail, .rc = rowCountResult.error()
    };
  }

  // files with no reads in this query (multi-aln only)
  std::vector<EmptyFile> newEmptyFiles;
  if (locusInfo.nAln > 1) {
    // precondition:
    // - query is sorted by path as primary key
    std::vector<std::string> matchedPaths;
    sqlite3_reset (newStmt);
    int matchRc;
    while ((matchRc = sqlite3_step (newStmt)) == SQLITE_ROW) {
      const std::string_view rowPath{
          reinterpret_cast<const char*> (sqlite3_column_text (
              newStmt, schema::UserReadViewSelect::path
          )),
          static_cast<size_t> (sqlite3_column_bytes (
              newStmt, schema::UserReadViewSelect::path
          ))
      };
      if (matchedPaths.empty() || matchedPaths.back() != rowPath) {
        matchedPaths.emplace_back (rowPath);
      }
    }
    SQ3_TRY_OR_UNREACHABLE (
        matchRc, SQLITE_DONE,
        fmt::format (
            "failed to step query for matching files: {}",
            sqlite3_errmsg (conn)
        )
    );
    sqlite3_reset (newStmt);

    sqlite3_stmt* o_fileStmt = nullptr;
    Defer fileStmtCleanup ([&o_fileStmt]() {
      sqlite3_finalize (o_fileStmt);
    });
    SQ3_TRY_OR_UNREACHABLE (
        sqlite3_prepare_v2 (
            conn, schema::FileTableSelect::sql.data(),
            static_cast<int> (schema::FileTableSelect::sql.size()),
            &o_fileStmt, NULL
        ),
        SQLITE_OK,
        fmt::format (
            "failed to prepare file select statement: {}",
            sqlite3_errmsg (conn)
        )
    );
    int fileRc;
    while ((fileRc = sqlite3_step (o_fileStmt)) == SQLITE_ROW) {
      std::string path (
          reinterpret_cast<const char*> (sqlite3_column_text (
              o_fileStmt, schema::FileTableSelect::path
          )),
          static_cast<size_t> (sqlite3_column_bytes (
              o_fileStmt, schema::FileTableSelect::path
          ))
      );
      if (std::find (begin (matchedPaths), end (matchedPaths), path) !=
          end (matchedPaths)) {
        continue;
      }
      EmptyFileReason reason = EmptyFileReason::noMatchingReads;
      if (sqlite3_column_int (
              o_fileStmt, schema::FileTableSelect::hasContig
          ) == 0) {
        reason = EmptyFileReason::contigAbsent;
      }
      else if (sqlite3_column_int (
                   o_fileStmt, schema::FileTableSelect::hasReadsAtLocus
               ) == 0) {
        reason = EmptyFileReason::noReadsAtLocus;
      }
      newEmptyFiles.push_back ({std::move (path), reason});
    }
    SQ3_TRY_OR_UNREACHABLE (
        fileRc, SQLITE_DONE,
        fmt::format (
            "failed to step file select statement: {}",
            sqlite3_errmsg (conn)
        )
    );
  }
  emptyFiles = std::move (newEmptyFiles);

  sqlite3_finalize (selectStmt);
  selectStmt = newStmt;
  userClause = std::move (clause);
  stmtRowScrollOffset = 0;  // reset row view
  nStmtRows = *rowCountResult;
  return {.code = SetQueryStatus::success};
}

}  // namespace g_state::db
