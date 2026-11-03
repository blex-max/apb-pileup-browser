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

#include "model/schema.hpp"
#include "shared/cleanup.hpp"
#include "tui_assert.hpp"

static std::string_view sanitise_clause (std::string_view clause)
{
  // trim whitespace, semicolons
  while (!clause.empty() && clause.front() == ' ') {
    clause.remove_prefix (1);
  }
  while (!clause.empty() && clause.back() == ' ') {
    clause.remove_suffix (1);
  }
  while (!clause.empty() && clause.back() == ';') {
    clause.remove_suffix (1);
  }
  return clause;
}

void update_where (
    QueryFragments& query, std::string_view cond,
    std::optional<Conjunctions> conjunction
)
{
  auto safeCond = std::string{sanitise_clause (cond)};
  if (safeCond.empty()) {
    return;
  }
  auto& where = query.where;

  if (conjunction) {
    // True if s has an OR token outside parens and quoted text.
    const auto hasTopLevelOr = [] (std::string_view s) {
      const auto isIdentChar = [] (char c) {
        const auto u = static_cast<unsigned char> (c);
        return std::isalnum (u) != 0 || u == '_' || u == '$' || u >= 0x80;
      };
      const auto upper = [] (char c) {
        return std::toupper (static_cast<unsigned char> (c));
      };
      int depth = 0;
      char quote = '\0';
      for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quote != '\0') {
          if (c == quote) {
            quote = '\0';
          }
        }
        else if (c == '\'' || c == '"' || c == '`') {
          quote = c;
        }
        else if (c == '(') {
          ++depth;
        }
        else if (c == ')') {
          depth = std::max (depth - 1, 0);
        }
        else if (depth == 0 && upper (c) == 'O' && i + 1 < s.size() &&
                 upper (s[i + 1]) == 'R' &&
                 (i == 0 || !isIdentChar (s[i - 1])) &&
                 (i + 2 == s.size() || !isIdentChar (s[i + 2]))) {
          return true;
        }
      }
      return false;
    };
    const auto paren_wrap = [] (std::string& s) {
      s.insert (0, 1, '(');
      s.append (")");
    };

    const bool isAnd = conjunction == Conjunctions::AND;
    // OR is the only operator lower in precendce than AND.
    // To preserve meaning, any clauses containing OR must be
    // paren wrapped when adding a new condition via AND.
    // Comments (--) also would break queries. By wrapping
    // they are forced to fail
    if ((isAnd && hasTopLevelOr (where)) || where.contains ("--")) {
      paren_wrap (where);
    }
    if (isAnd && hasTopLevelOr (safeCond)) {
      paren_wrap (safeCond);
    }
    where.append (isAnd ? " AND " : " OR ");
    where.append (safeCond);
  }
  else {
    where = safeCond;
  }
}

void update_order (QueryFragments& query, std::string_view order)
{
  const auto safeOrder = sanitise_clause (order);
  query.orderBy = safeOrder;
}

std::string to_display_string (const QueryFragments& query)
{
  std::string out;
  if (!query.where.empty()) {
    out.append ("WHERE ");
    out.append (query.where);
  }
  if (!query.orderBy.empty()) {
    if (!out.empty()) {
      out.append (" ");
    }
    out.append ("ORDER BY ");
    out.append (query.orderBy);
  }
  return out;
}

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

SetQueryStatus set_query (QueryFragments query)
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
  userQuery = std::move (query);
  stmtRowScrollOffset = 0;  // reset row view
  nStmtRows = static_cast<uint64_t> (queryRowCount);

  return SetQueryStatus::success;
}

}  // namespace tui_state::db

#ifndef DOCTEST_CONFIG_DISABLE

#include "doctest.h"
#include "fixtures.hpp"
#include "model/hts2apb.hpp"

TEST_CASE ("update_where paren-wraps correctly")
{
  QueryFragments q;
  update_where (q, "flag = 99");
  CHECK (q.where == "flag = 99");

  update_where (q, "b", Conjunctions::OR);
  update_where (q, "c", Conjunctions::AND);
  CHECK (q.where == "(flag = 99 OR b) AND c");

  QueryFragments q1;
  update_where (q1, "flag = 99 OR flag = 3");
  CHECK (q1.where == "flag = 99 OR flag = 3");
  update_where (q1, "basequal > 35 OR mapq > 0", Conjunctions::AND);
  CHECK (
      q1.where == "(flag = 99 OR flag = 3) AND (basequal > 35 OR mapq > 0)"
  );
}

TEST_CASE ("update_where wraps only where precedence requires")
{
  const auto A = Conjunctions::AND;
  const auto O = Conjunctions::OR;
  const auto joined = [] (std::string_view prev, std::string_view cond,
                          Conjunctions c) {
    QueryFragments q;
    update_where (q, prev);
    update_where (q, cond, c);
    return q.where;
  };

  // nothing to wrap
  CHECK (joined ("a", "b", A) == "a AND b");
  CHECK (joined ("a AND b", "c AND d", O) == "a AND b OR c AND d");
  CHECK (joined ("a OR b", "c OR d", O) == "a OR b OR c OR d");
  CHECK (
      joined ("a", "x BETWEEN 1 AND 2", A) == "a AND x BETWEEN 1 AND 2"
  );

  // OR on either side of an AND
  CHECK (joined ("a OR b", "c", A) == "(a OR b) AND c");
  CHECK (joined ("a", "b OR c", A) == "a AND (b OR c)");
  CHECK (joined ("a OR b", "c OR d", A) == "(a OR b) AND (c OR d)");

  // an earlier wrap is not wrapped again
  QueryFragments q;
  update_where (q, "x OR y");
  update_where (q, "z", A);
  update_where (q, "w", A);
  CHECK (q.where == "(x OR y) AND z AND w");

  // OR token detection: case, delimiters
  CHECK (joined ("a", "b or c", A) == "a AND (b or c)");
  CHECK (joined ("a", "b Or c", A) == "a AND (b Or c)");
  CHECK (joined ("a", "b OR(c)", A) == "a AND (b OR(c))");
  CHECK (joined ("a", "(b)OR(c)", A) == "a AND ((b)OR(c))");
  CHECK (joined ("a", "x='p'OR y='q'", A) == "a AND (x='p'OR y='q')");
  CHECK (joined ("a", "b\tOR\tc", A) == "a AND (b\tOR\tc)");

  // not OR tokens, or not top-level
  CHECK (joined ("a", "ORDER_x > 0", A) == "a AND ORDER_x > 0");
  CHECK (joined ("a", "color > 0", A) == "a AND color > 0");
  CHECK (joined ("a", "x_or > 0", A) == "a AND x_or > 0");
  CHECK (joined ("a", "qname = 'x OR y'", A) == "a AND qname = 'x OR y'");
  CHECK (joined ("a", "(b OR c)", A) == "a AND (b OR c)");
  CHECK (joined ("(a OR b)", "c", A) == "(a OR b) AND c");

  // a comment in the old clause is wrapped, so the new term can't be
  // swallowed by it
  CHECK (joined ("a -- note", "b", A) == "(a -- note) AND b");
  CHECK (joined ("a -- note", "b", O) == "(a -- note) OR b");
}

TEST_CASE ("update_where and update_order sanitise their input")
{
  QueryFragments q;
  update_where (q, " ; ");
  update_order (q, "  ");
  CHECK (q.where.empty());
  CHECK (q.orderBy.empty());

  update_where (q, "  flag = 99; ");
  update_order (q, "  mapq DESC; ");
  CHECK (q.where == "flag = 99");
  CHECK (q.orderBy == "mapq DESC");
}

TEST_CASE ("to_display_string")
{
  CHECK (to_display_string ({}).empty());
  CHECK (to_display_string ({.where = "a"}) == "WHERE a");
  CHECK (to_display_string ({.orderBy = "b"}) == "ORDER BY b");
  CHECK (
      to_display_string ({.where = "a", .orderBy = "b"}) ==
      "WHERE a ORDER BY b"
  );
}

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
  CHECK (g_db::userQuery.where == "flag = 99");
  CHECK (fetch_page (0, 10) == pageBefore);
}

TEST_CASE ("set_query replaces the previous query")
{
  DbFixture fx;
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
  DbFixture fx (
      {{"no_reads.bam", true},
       {"no_contig.bam", false},
       {"has_reads.bam", true}}
  );
  copy_reads_to_file (4);

  // nothing matches: all four files are empty, file 1 and 4 only by query
  REQUIRE (
      g_db::set_query ({.where = {"qname = 'none'"}}) ==
      g_db::SetQueryStatus::success
  );
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
  REQUIRE (g_db::set_query ({}) == g_db::SetQueryStatus::success);
  CHECK (g_db::queryEmptyFiles.size() == 2);
}

TEST_CASE ("set_query keeps path as the primary sort key")
{
  DbFixture fx ({{"b.bam", true}});
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
  DbFixture fx;
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
