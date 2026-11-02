#include "query.hpp"

#include <fmt/format.h>
#include <sqlite3.h>

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

void update_where (QueryFragments& query, std::string_view cond)
{
  const auto safeCond = sanitise_clause (cond);
  if (safeCond.empty()) {
    return;
  }
  auto& where = query.where;

  if (where.empty()) {
    where = safeCond;
  }
  else {
    where.insert (0, 1, '(');
    where.append (") ");
    where.append (safeCond);
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

TEST_CASE ("update_where left-wraps earlier conditions in parens")
{
  QueryFragments q;
  update_where (q, "flag = 99");
  CHECK (q.where == "flag = 99");

  update_where (q, "OR b");
  update_where (q, "AND c");
  CHECK (q.where == "((flag = 99) OR b) AND c");
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
