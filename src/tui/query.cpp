#include "query.hpp"

#include <fmt/format.h>
#include <sqlite3.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "model/schema.hpp"
#include "shared/cleanup.hpp"
#include "tui_assert.hpp"

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

  out.pos = sqlite3_column_int64 (o_stmt, schema::MetaTableSelect::pos);
  out.start =
      sqlite3_column_int64 (o_stmt, schema::MetaTableSelect::start);
  out.end = sqlite3_column_int64 (o_stmt, schema::MetaTableSelect::end);

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
      sqlite3_column_int (o_stmt, schema::MetaTableSelect::n_aln)
  );

  return out;
}

namespace tui_state::db {

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

  // order by path is mandatory for purposes
  // of multi-alignment view. Irrelevant
  // for single-file view.
  userQuerySuffix.append (" ORDER BY path");
  if (!query.orderBy.empty()) {
    userQuerySuffix.append (", ");
    userQuerySuffix.append (query.orderBy);
    userQuerySuffix.append ("\n");
  }

  // NOTE: a semicolon in the query could cause issues,
  // but it's essentially intentional misuse so we can ignore
  {
    const auto sqlCreateTempTable = fmt::format (
        "CREATE TEMP TABLE new_ordered_query AS SELECT id, path_id FROM "
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
    // FIXME: should path_id be indexed to speed up table creation?
    if (const auto rc = sqlite3_step (o_createTblStmt);
        rc != SQLITE_DONE) {
      return rc;
    }
  }
  // NOTE: user sql is now validated

  // Statement may need resetting before drop
  sqlite3_reset (selectStmt);

  // Materialise temp table of ordered rowids for reads view
  // to avoid paying the cost of sorting every frame.
  if (const auto rc = sqlite3_exec (
          conn,
          "DROP TABLE IF EXISTS ordered_query;"
          "ALTER TABLE new_ordered_query RENAME TO ordered_query;",
          NULL, NULL, NULL
      );
      rc != SQLITE_OK) {
    // DROP will fail if there are any active statements
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

  if (locusInfo.nAln > 1) {
    /* check which alignments have reads */
    std::vector<EmptyFile> emptyForQuery;
    sqlite3_stmt* o_alnSelStmt = nullptr;
    TUI_S3_EXPECT (
        sqlite3_prepare_v2 (
            conn,
            "SELECT a.path, a.has_contig, "
            "EXISTS (SELECT 1 FROM per_record_data AS r WHERE r.path_id = "
            "a.id) AS has_any_reads FROM alignment_files AS a LEFT JOIN "
            "(SELECT "
            "DISTINCT path_id FROM ordered_query) AS o ON o.path_id = "
            "a.id "
            "WHERE o.path_id IS NULL ORDER BY a.id;",
            -1, &o_alnSelStmt, NULL
        ),
        SQLITE_OK
    );
    Defer alnSelStmt_cleanup ([&o_alnSelStmt]() {
      sqlite3_finalize (o_alnSelStmt);
    });
    int rc;
    while ((rc = sqlite3_step (o_alnSelStmt)) == SQLITE_ROW) {
      const std::string path{
          reinterpret_cast<const char*> (
              sqlite3_column_text (o_alnSelStmt, 0)
          ),
          static_cast<size_t> (sqlite3_column_bytes (o_alnSelStmt, 0))
      };
      const bool hasContig =
          static_cast<bool> (sqlite3_column_int (o_alnSelStmt, 1));
      const bool hasAnyReads =
          static_cast<bool> (sqlite3_column_int (o_alnSelStmt, 2));

      // NOTE: any file which does not have locus reads/contig
      // will always retain the same reason, so maybe this is
      // suboptimal. But it will do.
      if (hasAnyReads) {
        emptyForQuery.emplace_back (
            path, EmptyFileReason::noMatchingReads
        );
      }
      else if (hasContig) {
        emptyForQuery.emplace_back (path, EmptyFileReason::noReadsAtLocus);
      }
      else {
        emptyForQuery.emplace_back (path, EmptyFileReason::contigAbsent);
      }
    }
    TUI_ASSERT (rc == SQLITE_DONE);

    queryEmptyFiles = std::move (emptyForQuery);
  }

  sqlite3_stmt* br_newStmt = nullptr;
  // FIXME: It is likely worthwhile for performance to
  // select only the columns necessary for each frame.
  // E.g. tags as of 21/10/26 can't be displayed anyway,
  // so why are we selecting it!
  // This will need additional machinery for getting
  // the right columns on retrieval though.
  TUI_S3_EXPECT (
      sqlite3_prepare_v2 (
          conn,
          "SELECT r.* FROM ordered_query o JOIN reads r ON r.id = o.id "
          "WHERE o.rowid > ?1 ORDER BY o.rowid LIMIT ?2;",
          -1, &br_newStmt, NULL
      ),
      SQLITE_OK
  );

  sqlite3_finalize (selectStmt);
  selectStmt = br_newStmt;

  while (queryUndoStack.size() > 7) {
    queryUndoStack.erase (begin (queryUndoStack));
  }
  queryUndoStack.push_back (userQuery);
  userQuery = query;
  stmtRowScrollOffset = 0;  // reset row view
  nStmtRows = static_cast<uint64_t> (queryRowCount);

  return SQLITE_OK;
}

}  // namespace tui_state::db

#ifndef DOCTEST_CONFIG_DISABLE

#include "doctest.h"
#include "fixtures.hpp"
#include "model/hts2apb.hpp"

namespace {

namespace g_db = tui_state::db;

// owns the tui_state::db globals for the duration of a test
struct DbFixture {
  explicit DbFixture (const std::vector<test::ExtraFile>& extraFiles = {})
  {
    g_db::conn = test::new_db();
    test::seed_reads (g_db::conn, extraFiles);
    g_db::locusInfo = get_locus_data (g_db::conn);
  }
  ~DbFixture()
  {
    sqlite3_finalize (g_db::selectStmt);
    g_db::selectStmt = nullptr;
    sqlite3_close_v2 (g_db::conn);
    g_db::conn = nullptr;
    g_db::userQuery = {};
    g_db::queryEmptyFiles.clear();
    g_db::nStmtRows = 0;
  }
  DbFixture (const DbFixture&) = delete;
  DbFixture& operator= (const DbFixture&) = delete;
};

void exec (const char* sql) { test::exec_ok (g_db::conn, sql); }

// duplicate file 1's reads into file `id`
void copy_reads_to_file (int id)
{
  exec (
      fmt::format (
          "INSERT INTO per_record_data (qname, flag, start, end, mapq, "
          "base, basequal, qpos, indel, is_del, is_head, is_tail, "
          "is_refskip, cigar, seq, qual, mtid, mstart, tags, cig_uint32, "
          "ncig, path_id) SELECT qname, flag, start, end, mapq, base, "
          "basequal, qpos, indel, is_del, is_head, is_tail, is_refskip, "
          "cigar, seq, qual, mtid, mstart, tags, cig_uint32, ncig, {} "
          "FROM per_record_data WHERE path_id = 1;",
          id
      )
          .c_str()
  );
}

// qnames of the page the render loop would fetch
std::vector<std::string> fetch_page (int64_t offset, int64_t limit)
{
  sqlite3_stmt* stmt = g_db::selectStmt;
  sqlite3_reset (stmt);
  sqlite3_bind_int64 (stmt, 1, offset);
  sqlite3_bind_int64 (stmt, 2, limit);
  std::vector<std::string> out;
  while (sqlite3_step (stmt) == SQLITE_ROW) {
    out.emplace_back (
        reinterpret_cast<const char*> (
            sqlite3_column_text (stmt, schema::UserReadViewSelect::qname)
        )
    );
  }
  sqlite3_reset (stmt);
  return out;
}

std::string row_path (sqlite3_stmt* stmt)
{
  return reinterpret_cast<const char*> (
      sqlite3_column_text (stmt, schema::UserReadViewSelect::path)
  );
}

}  // namespace

TEST_CASE ("set_query failure leaves the active query untouched")
{
  DbFixture fx;
  REQUIRE (
      g_db::set_query ({.where = {"flag = 99"}, .orderBy = ""}) ==
      SQLITE_OK
  );
  const auto* const stmtBefore = g_db::selectStmt;
  const auto pageBefore = fetch_page (0, 10);

  CHECK (g_db::set_query ({.where = {"nonsense ="}}) != SQLITE_OK);

  CHECK (g_db::selectStmt == stmtBefore);
  CHECK (g_db::nStmtRows == 2);
  CHECK (g_db::userQuery.where == "flag = 99");
  CHECK (fetch_page (0, 10) == pageBefore);
}

TEST_CASE ("set_query replaces the previous query")
{
  DbFixture fx;
  REQUIRE (g_db::set_query ({}) == SQLITE_OK);
  REQUIRE (g_db::nStmtRows == 3);
  // leave the old stmt mid-iteration, as render does when the page fills
  sqlite3_reset (g_db::selectStmt);
  sqlite3_bind_int64 (g_db::selectStmt, 1, 0);
  sqlite3_bind_int64 (g_db::selectStmt, 2, 10);
  REQUIRE (sqlite3_step (g_db::selectStmt) == SQLITE_ROW);

  REQUIRE (g_db::set_query ({.where = {"qname = 'readB'"}}) == SQLITE_OK);
  CHECK (g_db::nStmtRows == 1);
  CHECK (fetch_page (0, 10) == std::vector<std::string>{"readB"});
}

TEST_CASE ("set_query classifies files with no reads in the query")
{
  DbFixture fx (
      {{"no_reads.bam", true},
       {"no_contig.bam", false},
       {"has_reads.bam", true}}
  );
  copy_reads_to_file (4);

  // nothing matches: all four files are empty, file 1 and 4 only by query
  REQUIRE (g_db::set_query ({.where = {"qname = 'none'"}}) == SQLITE_OK);
  CHECK (g_db::nStmtRows == 0);
  using R = g_db::EmptyFileReason;
  const auto& ef = g_db::queryEmptyFiles;
  REQUIRE (ef.size() == 4);
  CHECK (ef[0].path == "test.bam");
  CHECK (ef[0].reason == R::noMatchingReads);
  CHECK (ef[1].path == "no_reads.bam");
  CHECK (ef[1].reason == R::noReadsAtLocus);
  CHECK (ef[2].path == "no_contig.bam");
  CHECK (ef[2].reason == R::contigAbsent);
  CHECK (ef[3].reason == R::noMatchingReads);

  // files with matching reads are not listed
  REQUIRE (g_db::set_query ({}) == SQLITE_OK);
  CHECK (g_db::queryEmptyFiles.size() == 2);
}

TEST_CASE ("set_query keeps path as the primary sort key")
{
  DbFixture fx ({{"b.bam", true}});
  copy_reads_to_file (2);

  REQUIRE (g_db::set_query ({.orderBy = "mapq DESC"}) == SQLITE_OK);

  sqlite3_stmt* stmt = g_db::selectStmt;
  sqlite3_reset (stmt);
  sqlite3_bind_int64 (stmt, 1, 0);
  sqlite3_bind_int64 (stmt, 2, 10);
  std::vector<std::string> paths;
  while (sqlite3_step (stmt) == SQLITE_ROW) {
    paths.push_back (row_path (stmt));
  }
  sqlite3_reset (stmt);
  // grouped by file, not interleaved by mapq
  CHECK (
      paths ==
      std::vector<std::string>{
          "b.bam", "b.bam", "b.bam", "test.bam", "test.bam", "test.bam"
      }
  );
}

#endif  // DOCTEST_CONFIG_DISABLE
