#include "app/g_state.hpp"

#include <fmt/format.h>
#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "app/sql.hpp"
#include "backend/hts_sql.hpp"
#include "backend/schema.hpp"
#include "shared/apb_assert.hpp"
#include "shared/cleanup.hpp"

namespace g_state::ui {

constinit std::array<ColMetadata, 10> tableCols{{
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

SetQueryStatus set_query (sql::QueryFragments userFrags)
{
  // minor guard/cleanup of user sql
  for (auto& whereFrag : userFrags.where) {
    if (!whereFrag.empty() && whereFrag.back() == ';') {
      whereFrag.pop_back();
    }
  }
  if (!userFrags.orderBy.empty() && userFrags.orderBy.back() == ';') {
    userFrags.orderBy.pop_back();
  }

  std::string userQuerySuffix;
  // build WHERE
  if (!userFrags.where.empty()) {
    userQuerySuffix.append ("WHERE ");
    userQuerySuffix.append (sql::process_where (userFrags.where));
    // add newline to avoid issues from user sql e.g. trailing
    // -- comment
    userQuerySuffix.append ("\n");
  }

  // order by path is mandatory for purposes
  // of multi-alignment view. Irrelevant
  // for single-file view.
  userQuerySuffix.append (" ORDER BY path");
  if (!userFrags.orderBy.empty()) {
    userQuerySuffix.append (", ");
    userQuerySuffix.append (userFrags.orderBy);
    userQuerySuffix.append ("\n");
  }

  // FIXME - can qname or tag values contain ;? if so,
  // the user might want to legitmately use the ; char
  // in a query suffix...
  if (std::find (begin (userQuerySuffix), end (userQuerySuffix), ';') !=
      end (userQuerySuffix)) {
    return SetQueryStatus::guardFail;
  }

  {
    // NOTE: it is at this point that we first attempt usage of the
    // user-provided suffix, and the user sql is therefore validated.
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
    // NOTE: these probably don't need seperate return codes...
    if (const auto rc = sqlite3_prepare_v2 (
            conn, sqlCreateTempTable.data(),
            static_cast<int> (sqlCreateTempTable.size()), &o_createTblStmt,
            NULL
        );
        rc != SQLITE_OK) {
      return SetQueryStatus::s3Fail;
    }
    // FIXME: should path_id be indexed to speed up table creation?
    if (const auto rc = sqlite3_step (o_createTblStmt);
        rc != SQLITE_DONE) {
      return SetQueryStatus::s3Fail;
    }
  }
  // NOTE: user sql is now validated

  // Statement may need resetting before drop
  sqlite3_reset (selectStmt);

  // Materialise temp table of ordered rowids for reads view
  // to avoid paying the cost of sorting every frame.
  APB_S3_EXPECT_OK (sqlite3_exec (
      g_state::db::conn,
      "DROP TABLE IF EXISTS ordered_query;"
      "ALTER TABLE new_ordered_query RENAME TO ordered_query;",
      NULL, NULL, NULL
  ));

  int64_t queryRowCount = 0;
  {
    sqlite3_stmt* o_queryRowCountStmt = nullptr;
    APB_S3_EXPECT (
        sqlite3_prepare_v2 (
            conn, "SELECT COUNT(*) FROM ordered_query;", -1,
            &o_queryRowCountStmt, NULL
        ),
        SQLITE_OK
    );
    Defer cleanup ([&o_queryRowCountStmt]() {
      sqlite3_finalize (o_queryRowCountStmt);
    });
    APB_S3_EXPECT (sqlite3_step (o_queryRowCountStmt), SQLITE_ROW);
    queryRowCount = sqlite3_column_int64 (o_queryRowCountStmt, 0);
    APB_ASSERT (queryRowCount >= 0);
  }

  if (locusInfo.nAln > 1) {
    /* check which alignments have reads */
    std::vector<EmptyFile> emptyForQuery;
    sqlite3_stmt* o_alnSelStmt = nullptr;
    APB_S3_EXPECT_OK (sqlite3_prepare_v2 (
        conn,
        "SELECT a.path, a.has_contig, a.has_reads FROM "
        "alignment_files AS a LEFT JOIN (SELECT DISTINCT path_id FROM "
        "ordered_query) AS o ON o.path_id = a.id WHERE o.path_id IS "
        "NULL ORDER BY a.id;",
        -1, &o_alnSelStmt, NULL
    ));
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
      const bool hasReads =
          static_cast<bool> (sqlite3_column_int (o_alnSelStmt, 2));

      // NOTE: any file which does not have locus reads/contig
      // will always retain the same reason, so maybe this is
      // suboptimal. But it will do.
      if (hasReads) {
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
    APB_ASSERT (rc == SQLITE_DONE);

    emptyFiles = std::move (emptyForQuery);
  }

  sqlite3_stmt* br_newStmt = nullptr;
  // FIXME: It is likely worthwhile for performance to
  // select only the columns necessary for each frame.
  // E.g. tags as of 21/10/26 can't be displayed anyway,
  // so why are we selecting it!
  // This will need additional machinery for getting
  // the right columns on retrieval though.
  APB_S3_EXPECT (
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
  userClause = std::move (userFrags);
  stmtRowScrollOffset = 0;  // reset row view
  nStmtRows = static_cast<uint64_t> (queryRowCount);
  return SetQueryStatus::success;
}

}  // namespace g_state::db

#ifndef DOCTEST_CONFIG_DISABLE

#include "doctest.h"
#include "fixtures.hpp"

namespace {

namespace g_db = g_state::db;

// owns the g_state::db globals for the duration of a test
struct DbFixture {
  explicit DbFixture (int16_t nAln)
  {
    g_db::conn = setup_db::init();
    test::seed_reads (g_db::conn, nAln);
    g_db::locusInfo = sql::get_locus_data (g_db::conn);
  }
  ~DbFixture()
  {
    sqlite3_finalize (g_db::selectStmt);
    g_db::selectStmt = nullptr;
    sqlite3_close_v2 (g_db::conn);
    g_db::conn = nullptr;
    g_db::userClause = {};
    g_db::emptyFiles.clear();
    g_db::nStmtRows = 0;
  }
  DbFixture (const DbFixture&) = delete;
  DbFixture& operator= (const DbFixture&) = delete;
};

void exec (const char* sql)
{
  REQUIRE (
      sqlite3_exec (g_db::conn, sql, nullptr, nullptr, nullptr) ==
      SQLITE_OK
  );
}

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

void add_file (int id, const char* path, int hasContig, int hasReads)
{
  exec (
      fmt::format (
          "INSERT INTO alignment_files (id, path, has_contig, has_reads) "
          "VALUES ({}, '{}', {}, {});",
          id, path, hasContig, hasReads
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
  DbFixture fx (1);
  REQUIRE (
      g_db::set_query ({.where = {"flag = 99"}, .orderBy = ""}) ==
      g_db::SetQueryStatus::success
  );
  const auto* const stmtBefore = g_db::selectStmt;
  const auto pageBefore = fetch_page (0, 10);

  SUBCASE ("semicolon")
  {
    CHECK (
        g_db::set_query ({.where = {"flag = 1; DROP TABLE reads"}}) ==
        g_db::SetQueryStatus::guardFail
    );
  }
  SUBCASE ("invalid sql")
  {
    CHECK (
        g_db::set_query ({.where = {"nonsense ="}}) ==
        g_db::SetQueryStatus::s3Fail
    );
  }

  CHECK (g_db::selectStmt == stmtBefore);
  CHECK (g_db::nStmtRows == 2);
  CHECK (g_db::userClause.where == std::vector<std::string>{"flag = 99"});
  CHECK (fetch_page (0, 10) == pageBefore);
}

TEST_CASE ("set_query replaces the previous query")
{
  DbFixture fx (1);
  REQUIRE (g_db::set_query ({}) == g_db::SetQueryStatus::success);
  REQUIRE (g_db::nStmtRows == 3);
  // leave the old stmt mid-iteration, as render does when the page fills
  sqlite3_reset (g_db::selectStmt);
  sqlite3_bind_int64 (g_db::selectStmt, 1, 0);
  sqlite3_bind_int64 (g_db::selectStmt, 2, 10);
  REQUIRE (sqlite3_step (g_db::selectStmt) == SQLITE_ROW);

  REQUIRE (
      g_db::set_query ({.where = {"qname = 'readB'"}}) ==
      g_db::SetQueryStatus::success
  );
  CHECK (g_db::nStmtRows == 1);
  CHECK (fetch_page (0, 10) == std::vector<std::string>{"readB"});
}

TEST_CASE ("set_query classifies files with no reads in the query")
{
  DbFixture fx (4);
  add_file (2, "no_reads.bam", 1, 0);
  add_file (3, "no_contig.bam", 0, 0);
  add_file (4, "has_reads.bam", 1, 1);
  copy_reads_to_file (4);

  // nothing matches: all four files are empty, file 1 and 4 only by query
  REQUIRE (
      g_db::set_query ({.where = {"qname = 'none'"}}) ==
      g_db::SetQueryStatus::success
  );
  CHECK (g_db::nStmtRows == 0);
  using R = g_db::EmptyFileReason;
  const auto& ef = g_db::emptyFiles;
  REQUIRE (ef.size() == 4);
  CHECK (ef[0].path == "test.bam");
  CHECK (ef[0].reason == R::noMatchingReads);
  CHECK (ef[1].path == "no_reads.bam");
  CHECK (ef[1].reason == R::noReadsAtLocus);
  CHECK (ef[2].path == "no_contig.bam");
  CHECK (ef[2].reason == R::contigAbsent);
  CHECK (ef[3].reason == R::noMatchingReads);

  // files with matching reads are not listed
  REQUIRE (g_db::set_query ({}) == g_db::SetQueryStatus::success);
  CHECK (g_db::emptyFiles.size() == 2);
}

TEST_CASE ("set_query keeps path as the primary sort key")
{
  DbFixture fx (2);
  add_file (2, "b.bam", 1, 1);
  copy_reads_to_file (2);

  REQUIRE (
      g_db::set_query ({.orderBy = "mapq DESC"}) ==
      g_db::SetQueryStatus::success
  );

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

TEST_CASE ("selectStmt pages from the offset-th row, in view column order")
{
  DbFixture fx (1);
  REQUIRE (
      g_db::set_query ({.orderBy = "start"}) ==
      g_db::SetQueryStatus::success
  );
  CHECK (
      fetch_page (0, 10) ==
      std::vector<std::string>{"readA", "readB", "readC"}
  );
  CHECK (fetch_page (1, 10) == std::vector<std::string>{"readB", "readC"});
  CHECK (fetch_page (1, 1) == std::vector<std::string>{"readB"});

  sqlite3_stmt* stmt = g_db::selectStmt;
  sqlite3_reset (stmt);
  sqlite3_bind_int64 (stmt, 1, 0);
  sqlite3_bind_int64 (stmt, 2, 1);
  REQUIRE (sqlite3_step (stmt) == SQLITE_ROW);
  CHECK (row_path (stmt) == "test.bam");
  CHECK (
      sqlite3_column_int (stmt, schema::UserReadViewSelect::flag) == 99
  );
  sqlite3_reset (stmt);
}

#endif  // DOCTEST_CONFIG_DISABLE
