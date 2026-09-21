#pragma once

#include <fmt/format.h>
#include <htslib/sam.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "backend/hts_types.hpp"
#include "backend/sql_types.hpp"
#include "plog/Log.h"
#include "shared/err.hpp"

struct PileupDB : public SqliteConn {};
VoidOrErr init_db (PileupDB& db);

// insert provenance metadata into database (unlinked to loci/reads —
// one alignment file per db).
[[nodiscard]] VoidOrErr insert_metadata (
    PileupDB& db, const AlnFile& aln
);

// insert reads at pileup position into database
[[nodiscard]] VoidOrErr insert_pileup (
    PileupDB& db, const AlnFile& aln, const PileupPosition& pos,
    const std::optional<FastaFile>& ff
);

// Copy the in-memory database out to a file on disk, using
// sqlite3's online backup API.
[[nodiscard]] VoidOrErr dump_to_disk (
    const PileupDB& db, std::string_view path
);

// Serialize the in-memory database and write the raw bytes to
// stdout, for `--dump -`.
[[nodiscard]] VoidOrErr dump_to_stdout (const PileupDB& db);

// Copy a database file on disk into an in-memory PileupDB,
// using sqlite3's online backup API.
[[nodiscard]] VoidOrErr load_from_disk (
    PileupDB& db, std::string_view path
);

// locus metadata as extracted from db.
struct PileupMetadata {
  std::string contig;
  int64_t pos;  // 0-based pileup position, per loci.pos
  int64_t start;
  int64_t end;
  std::optional<std::string> refSlice;

  bool valid() const noexcept
  {
    return !contig.empty() && start >= 0 && start < end &&
           pos >= start && pos <= end;
  }
};
using LocusOrErr = std::expected<PileupMetadata, Err>;

PileupMetadata make_locus_data (
    std::string contigName, hts_pos_t pos,
    const GenomicSpan& span, std::optional<std::string> refSlice
);

// get locus data from pileup db.
LocusOrErr get_locus_data (const PileupDB& db);

// Steps statement forward by one row. Returns true if a row is available.
[[nodiscard]] BoolOrErr next_read (
    sqlite3_stmt* stmt, const PileupDB& db
);

struct DynamicSelectReadsStmt : public SqliteStmt {
  static inline const std::string_view sh_sqlPrefix =
      "SELECT * FROM reads";
};
struct DynamicFragments {
  std::vector<std::string> where;
  std::string orderBy;
};

using SelectStmtOrErr =
    std::expected<DynamicSelectReadsStmt, Err>;
inline SelectStmtOrErr prepare_select_reads (
    const PileupDB& db, const DynamicFragments& frags
)
{
  DynamicSelectReadsStmt stmt;

  std::string rsql_builtStmt{
      DynamicSelectReadsStmt::sh_sqlPrefix
  };

  // build WHERE
  if (!frags.where.empty()) {
    rsql_builtStmt.append (" WHERE ");
    for (size_t i = 0; i < frags.where.size(); ++i) {
      rsql_builtStmt.append (frags.where[i]);
      if (i != (frags.where.size() - 1)) {
        rsql_builtStmt.append (" ");
      }
    }
  }

  if (!frags.orderBy.empty()) {
    rsql_builtStmt.append (" ORDER BY ");
    rsql_builtStmt.append (frags.orderBy);
  }

  rsql_builtStmt.append (";");  // end stmt

  PLOGD << "Compiling user query: " + rsql_builtStmt;

  // Either of the following cases should be surfaced to the user

  int rc;
  if (rc = sqlite3_prepare_v2 (
          db, rsql_builtStmt.c_str(),
          static_cast<int> (rsql_builtStmt.size()), &stmt.o_stmt,
          NULL
      );
      rc != SQLITE_OK) {
    return std::unexpected{make_sqlite3_err (
        rc, fmt::format (
                "Could not compile statement: {} - {}",
                rsql_builtStmt, sqlite3_errmsg (db)
            )
    )};
  }

  if (sqlite3_stmt_readonly (stmt) == 0) {
    return std::unexpected{
        make_internal_err ("Statement would modify database.")
    };
  }

  return stmt;
};

struct DynamicCountReadsStmt : public SqliteStmt {
  static inline const std::string_view sh_sqlPrefix =
      "SELECT COUNT(*) FROM reads";
};

using CountStmtOrErr = std::expected<DynamicCountReadsStmt, Err>;
inline CountStmtOrErr prepare_count_reads (
    const PileupDB& db, const std::vector<std::string>& where
)
{
  DynamicCountReadsStmt stmt;

  std::string rsql_builtStmt{
      DynamicCountReadsStmt::sh_sqlPrefix
  };

  // build WHERE
  if (!where.empty()) {
    rsql_builtStmt.append (" WHERE ");
    for (size_t i = 0; i < where.size(); ++i) {
      rsql_builtStmt.append (where[i]);
      if (i != (where.size() - 1)) {
        rsql_builtStmt.append (" ");
      }
    }
  }

  rsql_builtStmt.append (";");  // end stmt

  PLOGD << "Compiling user query: " + rsql_builtStmt;

  // Either of the following cases should be surfaced to the user

  int rc;
  if (rc = sqlite3_prepare_v2 (
          db, rsql_builtStmt.c_str(),
          static_cast<int> (rsql_builtStmt.size()), &stmt.o_stmt,
          NULL
      );
      rc != SQLITE_OK) {
    return std::unexpected{make_sqlite3_err (
        rc, fmt::format (
                "Could not compile statement: {} - {}",
                rsql_builtStmt, sqlite3_errmsg (db)
            )
    )};
  }

  if (sqlite3_stmt_readonly (stmt) == 0) {
    return std::unexpected{
        make_internal_err ("Statement would modify database.")
    };
  }

  return stmt;
}
