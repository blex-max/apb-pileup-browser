#include "query.hpp"

#include <fmt/format.h>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

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
    const auto wrap = [] (std::string& s) {
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
      wrap (where);
    }
    if (isAnd && hasTopLevelOr (safeCond)) {
      wrap (safeCond);
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
  if (safeOrder.empty()) {
    return;
  }
  query.orderBy = safeOrder;
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

#ifndef DOCTEST_CONFIG_DISABLE

#include "doctest.h"

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

#endif  // DOCTEST_CONFIG_DISABLE
