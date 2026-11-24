#include "query.hpp"

#include <fmt/format.h>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "apb_db/apb_db_schema.hpp"
#include "sqlite_col.hpp"
#include "tui_assert.hpp"
#include "utility/cleanup.hpp"

std::string PileupMetadata::to_string() const
{
  return fmt::format (
      "PileupMetadata{{contig: {}, pos: {}, start: {}, end: {}}}", contig,
      pos, start, end
  );
}

PileupMetadata get_locus_data (sqlite3* conn)
{
  sqlite3_stmt* o_stmt = NULL;
  if (const auto rc = sqlite3_prepare_v2 (
          conn, schema::MetaTableSelect::sql.data(),
          static_cast<int> (schema::MetaTableSelect::sql.size()), &o_stmt,
          NULL
      );
      rc != SQLITE_OK) {
    TUI_UNREACHABLE (
        fmt::format (
            "failed to prepare locus metadata query: {}",
            sqlite3_errmsg (conn)
        )
    );
  }
  Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

  if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_ROW) {
    TUI_UNREACHABLE (
        fmt::format (
            "failed to read locus metadata row: {}", sqlite3_errmsg (conn)
        )
    );
  }

  PileupMetadata out;
  out.contig = {
      reinterpret_cast<const char*> (
          sqlite3_column_text (o_stmt, schema::MetaTableSelect::contig)
      ),
      static_cast<size_t> (
          sqlite3_column_bytes (o_stmt, schema::MetaTableSelect::contig)
      )
  };

  out.pos = sqlite3_column_int64 (
      o_stmt, schema::MetaTableSelect::pileup_position
  );
  out.start = sqlite3_column_int64 (
      o_stmt, schema::MetaTableSelect::records_start
  );
  out.end =
      sqlite3_column_int64 (o_stmt, schema::MetaTableSelect::records_end);

  if (sqlite3_column_type (o_stmt, schema::MetaTableSelect::ref) !=
      SQLITE_NULL) {
    out.refSlice = std::string{
        reinterpret_cast<const char*> (
            sqlite3_column_text (o_stmt, schema::MetaTableSelect::ref)
        ),
        static_cast<size_t> (
            sqlite3_column_bytes (o_stmt, schema::MetaTableSelect::ref)
        )
    };
    TUI_ASSERT (!out.refSlice.value().empty());
  }

  out.nAln = static_cast<int16_t> (
      sqlite3_column_int (o_stmt, schema::MetaTableSelect::n_sources)
  );

  return out;
}

std::vector<AlnFile> get_aln_files (sqlite3* conn)
{
  sqlite3_stmt* o_stmt = nullptr;
  TUI_S3_EXPECT (
      sqlite3_prepare_v2 (
          conn,
          "SELECT a.id, a.source, a.has_contig, COALESCE (t.n, 0) "
          "FROM record_sources AS a LEFT JOIN "
          "(SELECT source_id, COUNT(*) AS n FROM sam_records "
          "GROUP BY source_id) AS t ON t.source_id = a.id "
          "ORDER BY a.id;",
          -1, &o_stmt, NULL
      ),
      SQLITE_OK
  );
  Defer stmt_cleanup ([&o_stmt]() { sqlite3_finalize (o_stmt); });

  std::vector<AlnFile> out;
  int rc;
  while ((rc = sqlite3_step (o_stmt)) == SQLITE_ROW) {
    out.push_back ({
        .id = sqlite3_column_int64 (o_stmt, 0),
        .path = col_text (o_stmt, 1).value_or ("*"),
        .hasContig = sqlite3_column_int (o_stmt, 2) != 0,
        .nTotal = static_cast<uint64_t> (sqlite3_column_int64 (o_stmt, 3)),
        .nQuery = 0,
    });
  }
  TUI_ASSERT (rc == SQLITE_DONE, rc);
  return out;
}

namespace tui_state::db {

// FIXME: it would be more convenient for testing etc.
// to take a context object of ptrs to all relevant globals
int set_query (const QueryFragments& query)
{
  std::string userQuerySuffix;
  // build WHERE
  if (!query.where.empty()) {
    userQuerySuffix.append ("WHERE ");
    userQuerySuffix.append (query.where);
    // add newline to avoid issues from user sql e.g. trailing
    // -- comment
    userQuerySuffix.append ("\n");
  }

  // order by source is mandatory for purposes
  // of multi-alignment view. Irrelevant
  // for single-file view.
  userQuerySuffix.append (" ORDER BY source");
  if (!query.orderBy.empty()) {
    userQuerySuffix.append (", ");
    userQuerySuffix.append (query.orderBy);
    userQuerySuffix.append ("\n");
  }

  {
    // Materialise temp table of ordered rowids of `reads` from user query,
    // to avoid paying the cost of sorting every frame.
    const auto sqlCreateTempTable = fmt::format (
        "CREATE TEMP TABLE new_ordered_query AS SELECT id, source_id FROM "
        "reads "
        "{}\n;",
        userQuerySuffix
    );
    sqlite3_stmt* o_createTblStmt = nullptr;
    Defer stmt_cleanup ([&o_createTblStmt]() {
      sqlite3_finalize (o_createTblStmt);
    });
    if (const auto rc = sqlite3_prepare_v2 (
            conn, sqlCreateTempTable.data(),
            static_cast<int> (sqlCreateTempTable.size()), &o_createTblStmt,
            NULL
        );
        rc != SQLITE_OK) {
      return rc;
    }
    // FIXME: should source_id be indexed to speed up table creation?
    if (const auto rc = sqlite3_step (o_createTblStmt);
        rc != SQLITE_DONE) {
      return rc;
    }
  }

  /* emplace new user query table */
  // We have created the ordered table for the new user query above
  // and it has successfully compiled. Now we can replace the previous
  // user query table with it (if present).
  sqlite3_reset (displayStmt);  // reset, since stmt references old table
  if (const auto rc = sqlite3_exec (
          conn,
          "DROP TABLE IF EXISTS ordered_query;"
          "ALTER TABLE new_ordered_query RENAME TO ordered_query;",
          NULL, NULL, NULL
      );
      rc != SQLITE_OK) {
    // DROP will fail if there are any active statements.
    // This shouldn't be possible - it is just defensive
    if (rc == SQLITE_LOCKED || rc == SQLITE_BUSY) {
      std::string busySql;
      for (auto* s = sqlite3_next_stmt (conn, nullptr); s != nullptr;
           s = sqlite3_next_stmt (conn, s)) {
        if (sqlite3_stmt_busy (s) != 0) {
          busySql += fmt::format ("\n  {}", sqlite3_sql (s));
        }
      }
      TUI_UNREACHABLE (
          "failed to drop previous query: active statements", rc,
          sqlite3_errstr (rc), sqlite3_errmsg (conn), busySql
      );
    }
    TUI_UNREACHABLE (
        "failed to drop previous query", rc, sqlite3_errstr (rc),
        sqlite3_errmsg (conn)
    );
  }

  /* count number of rows (reads) in new query */
  int64_t queryRowCount = 0;
  {
    sqlite3_stmt* o_queryRowCountStmt = nullptr;
    TUI_S3_EXPECT (
        sqlite3_prepare_v2 (
            conn, "SELECT COUNT(*) FROM ordered_query;", -1,
            &o_queryRowCountStmt, NULL
        ),
        SQLITE_OK
    );
    Defer cleanup ([&o_queryRowCountStmt]() {
      sqlite3_finalize (o_queryRowCountStmt);
    });
    TUI_S3_EXPECT (sqlite3_step (o_queryRowCountStmt), SQLITE_ROW);
    queryRowCount = sqlite3_column_int64 (o_queryRowCountStmt, 0);
    TUI_ASSERT (queryRowCount >= 0);
  }

  /* count reads per file in new query */
  if (!alnFiles.empty()) {
    for (auto& file : alnFiles) {
      file.nQuery = 0;
    }
    sqlite3_stmt* o_countStmt = nullptr;
    TUI_S3_EXPECT (
        sqlite3_prepare_v2 (
            conn,
            "SELECT source_id, COUNT(*) FROM ordered_query "
            "GROUP BY source_id;",
            -1, &o_countStmt, NULL
        ),
        SQLITE_OK
    );
    Defer countStmt_cleanup ([&o_countStmt]() {
      sqlite3_finalize (o_countStmt);
    });
    int rc;
    while ((rc = sqlite3_step (o_countStmt)) == SQLITE_ROW) {
      const auto id = sqlite3_column_int64 (o_countStmt, 0);
      const auto file = std::ranges::find (alnFiles, id, &AlnFile::id);
      TUI_ASSERT (file != alnFiles.end(), id);
      file->nQuery =
          static_cast<uint64_t> (sqlite3_column_int64 (o_countStmt, 1));
    }
    TUI_ASSERT (rc == SQLITE_DONE, rc);
  }

  sqlite3_stmt* br_newDisplayStmt = nullptr;
  // prepare statement wherein the starting row (offset) and number of rows to be retrieved can
  // be set by bind parameters `?1` and `?2` respectively, allowing
  // per-frame retrieval of only the tranche of reads which is
  // currently to be displayed on screen.
  TUI_S3_EXPECT (
      sqlite3_prepare_v2 (
          conn,
          "SELECT r.* FROM ordered_query o JOIN reads r ON r.id = o.id "
          "WHERE o.rowid > ?1 ORDER BY o.rowid LIMIT ?2;",
          -1, &br_newDisplayStmt, NULL
      ),
      SQLITE_OK
  );

  sqlite3_finalize (displayStmt);
  displayStmt = br_newDisplayStmt;

  while (queryUndoStack.size() > 7) {
    queryUndoStack.erase (begin (queryUndoStack));
  }
  queryUndoStack.push_back (userQuery);
  userQuery = query;
  displayScrollOffset = 0;  // reset row view
  nQueryRows = static_cast<uint64_t> (queryRowCount);

  return SQLITE_OK;
}

}  // namespace tui_state::db

#ifndef DOCTEST_CONFIG_DISABLE

#include "apb_db/apb_db_core.hpp"
#include "doctest.h"
#include "fixtures.hpp"

namespace {

namespace g_db = tui_state::db;

// qnames of the page the render loop would fetch
std::vector<std::string> fetch_page_qnames (int64_t offset, int64_t limit)
{
  sqlite3_stmt* stmt = g_db::displayStmt;
  sqlite3_reset (stmt);
  sqlite3_bind_int64 (stmt, 1, offset);
  sqlite3_bind_int64 (stmt, 2, limit);
  std::vector<std::string> out;
  while (sqlite3_step (stmt) == SQLITE_ROW) {
    out.emplace_back (
        col_text (stmt, schema::UserReadViewSelect::qname).value_or ("*")
    );
  }
  sqlite3_reset (stmt);
  return out;
}

}  // namespace

// FIXME: complete implemententation here!
TEST_CASE ("set_query behaviour")
{
  Defer backstop_cleanup ([]() { sqlite3_close_v2 (g_db::conn); });
  SUBCASE ("Behaviour expectatations in normal use")
  {
    // wipe any existing data, set up fresh db for test
    // TODO: macro
    sqlite3_finalize (g_db::displayStmt);
    g_db::displayStmt = nullptr;
    sqlite3_close_v2 (g_db::conn);
    g_db::conn = nullptr;
    REQUIRE (sqlite3_open (":memory:", &g_db::conn) == SQLITE_OK);
    Defer db_cleanup ([]() { sqlite3_close_v2 (g_db::conn); });
    REQUIRE (apb_create_schema (g_db::conn) == SQLITE_OK);
    //FIXME get locus data
#define DEF_TEST_REC(test_qname, test_mapq) \
  test::TestRecord{                         \
      .qname = (test_qname),                \
      .flag = 99,                           \
      .mapQ = (test_mapq),                  \
      .start_0ho = 100,                     \
      .end_0ho = 200                        \
  }
    const auto testAlns = std::to_array<test::TestAln> ({
        test::TestAln{
            "test1.bam",
            std::vector<test::TestRecord>{
                DEF_TEST_REC ("read1_f1", 51),
                DEF_TEST_REC ("read2_f1", 50),
                DEF_TEST_REC ("read3_f1", 49),
            },
            true
        },
        test::TestAln{
            "test2.bam",
            std::vector<test::TestRecord>{
                DEF_TEST_REC ("read1_f2", 30),
                DEF_TEST_REC ("read2_f2", 70),
                DEF_TEST_REC ("read3_f2", 80),
            },
            true
        },
        test::TestAln{"no_reads.bam", std::nullopt, true},
        test::TestAln{"no_contig.bam", std::nullopt, false},
    });
#undef DEF_TEST_REC

    test::seed_conn_with_reads (
        g_db::conn, testAlns, "test-chr", 150, true
    );

    SUBCASE ("Basic where works as expected during paging")
    {
      REQUIRE (g_db::set_query ({}) == SQLITE_OK);
      REQUIRE (g_db::nQueryRows == 6);

      // leave the old stmt mid-iteration, as render does when the page fills
      sqlite3_reset (g_db::displayStmt);
      sqlite3_bind_int64 (g_db::displayStmt, 1, 0);
      sqlite3_bind_int64 (g_db::displayStmt, 2, 10);
      REQUIRE (sqlite3_step (g_db::displayStmt) == SQLITE_ROW);

      REQUIRE (
          g_db::set_query (
              {.where = {"qname = 'read2_f1'"}, .orderBy = {}}
          ) == SQLITE_OK
      );
      CHECK (g_db::nQueryRows == 1);
      CHECK (
          fetch_page_qnames (0, 10) == std::vector<std::string>{"read2_f1"}
      );
    }
    SUBCASE ("ordering uses source as primary sort key")
    {
      REQUIRE (
          g_db::set_query ({.where = {}, .orderBy = {"mapq DESC"}}) ==
          SQLITE_OK
      );
      CHECK (g_db::nQueryRows == 6);
      CHECK (
          fetch_page_qnames (0, 10) == std::vector<std::string>{
                                           "read1_f1",
                                           "read2_f1",
                                           "read3_f1",
                                           "read3_f2",
                                           "read2_f2",
                                           "read1_f2",
                                       }
      );
    }
    SUBCASE ("failure leaves active query untouched")
    {
      const auto* const stmtBefore = g_db::displayStmt;
      const auto pageBefore = fetch_page_qnames (0, 10);
      const auto nRowsBefore = g_db::nQueryRows;

      REQUIRE (
          g_db::set_query ({.where = {"nonsense &Daflkj ="}}) != SQLITE_OK
      );

      CHECK (g_db::displayStmt == stmtBefore);
      CHECK (g_db::nQueryRows == nRowsBefore);
      CHECK (fetch_page_qnames (0, 10) == pageBefore);
    }
    SUBCASE ("per-file read counts")
    {
      g_db::alnFiles = get_aln_files (g_db::conn);
      Defer files_cleanup ([]() { g_db::alnFiles.clear(); });
      const auto& files = g_db::alnFiles;
      REQUIRE (files.size() == 4);
      CHECK (files[2].path == "no_reads.bam");
      CHECK (files[2].hasContig);
      CHECK_FALSE (files[3].hasContig);
      CHECK (files[0].nTotal == 3);
      CHECK (files[1].nTotal == 3);
      CHECK (files[2].nTotal == 0);
      CHECK (files[3].nTotal == 0);

      REQUIRE (
          g_db::set_query ({.where = {"qname = 'read2_f1'"}}) == SQLITE_OK
      );
      CHECK (files[0].nQuery == 1);
      CHECK (files[1].nQuery == 0);
      // 1 read, then 3 files without reads
      CHECK (g_db::nScrollRows() == 4);
    }
  }
}

#endif  // DOCTEST_CONFIG_DISABLE
