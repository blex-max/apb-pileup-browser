#include "query.hpp"

#include <fmt/format.h>
#include <sqlite3.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "model/schema.hpp"
#include "shared/cleanup.hpp"
#include "state.hpp"

std::string process_where (const std::vector<std::string>& where)
{
  if (where.empty()) {
    return {};
  }
  std::string out = where[0];
  for (size_t i = 1; i < where.size(); ++i) {
    out.insert (0, "(");
    out += ") ";
    out += where[i];
  }
  return out;
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
  }

  out.nAln = static_cast<int16_t> (
      sqlite3_column_int (o_stmt, schema::MetaTableSelect::n_aln)
  );

  return out;
}

#ifndef DOCTEST_CONFIG_DISABLE

#include "doctest.h"

TEST_CASE ("process_where left-wraps fragments in parens")
{
  CHECK (process_where ({}) == "");
  CHECK (process_where ({"flag = 99"}) == "flag = 99");

  CHECK (process_where ({"a", "OR b", "AND c"}) == "((a) OR b) AND c");
}

#endif  // DOCTEST_CONFIG_DISABLE
