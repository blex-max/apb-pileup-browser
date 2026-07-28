#include "backend/PileupDB.hpp"

#include <fmt/format.h>
#include <htslib/sam.h>

#include <expected>
#include <string>

#include "backend/hts_types.hpp"
#include "backend/pileup_ingest.hpp"
#include "backend/sql.hpp"
#include "plog/Log.h"
#include "shared/err.hpp"

VoidOrErr init_db (PileupDB& db)
{
  int sqlRc = 0;

  if (sqlRc = sqlite3_open (":memory:", &db.o_ptr);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  auto excFn = [&db] (const std::string_view stmt) -> int {
    return sqlite3_exec (db, stmt.data(), NULL, NULL, NULL);
  };

  if (sqlRc = excFn (rsql_SetTempStoreMemory);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  if (sqlRc = excFn (rsql_PragmaForeignKeys);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  if (sqlRc = excFn (rsql_CreateMetaDataTable);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  if (sqlRc = excFn (rsql_CreateLociTable); sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  if (sqlRc = excFn (rsql_CreateReadsTable);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  if (sqlRc = excFn (rsql_CreateReadsLociIdIndex);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  return {};
}

VoidOrErr dump_to_disk (
    const PileupDB& db, const std::string& path
)
{
  /*
    Copy the in-memory database out to a file on disk,
    via sqlite3's online backup API.
  */
  int sqlRc = SQLITE_OK;
  sqlite3* o_fileDb = NULL;
  sqlite3_backup* o_backup = NULL;

  if (sqlRc = sqlite3_open (path.c_str(), &o_fileDb);
      sqlRc != SQLITE_OK) {
    goto err_sql;
  }

  if (o_backup =
          sqlite3_backup_init (o_fileDb, "main", db, "main");
      o_backup == NULL) {
    sqlRc = sqlite3_errcode (o_fileDb);
    goto err_sql;
  }

  // -1: copy all remaining pages in a single step.
  // ASSUMPTION: db is quiescent (no concurrent writer holding
  // a lock) for the duration of the copy.
  if (sqlRc = sqlite3_backup_step (o_backup, -1);
      sqlRc != SQLITE_DONE) {
    sqlite3_backup_finish (o_backup);
    goto err_sql;
  }

  if (sqlRc = sqlite3_backup_finish (o_backup);
      sqlRc != SQLITE_OK) {
    goto err_sql;
  }
  o_backup = NULL;  // fin

  if (sqlRc = sqlite3_close_v2 (o_fileDb); sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errstr (sqlRc))
    };
  }
  o_fileDb = NULL;

  return {};

err_sql: {
  // NOTE: per sqlite3 docs, errors from backup_init/backup_step
  // are stored on the *destination* handle, so o_fileDb is the
  // right handle to query here in every failure case above.
  const std::string errMsg = sqlite3_errmsg (o_fileDb);
  sqlite3_close_v2 (o_fileDb);
  return std::unexpected{make_sqlite3_err (sqlRc, errMsg)};
}
}

namespace {

// Concatenate every table/index definition in `db`'s schema into one
// string, in a deterministic order. Two connections built from the same
// DDL produce byte-identical output; any drift (missing table, added or
// removed column, changed constraint) shows up as a difference.
std::expected<std::string, Err> schema_fingerprint (PileupDB& db)
{
  sqlite3_stmt* o_stmt = NULL;
  int sqlRc = sqlite3_prepare_v2 (
      db,
      "SELECT type || ':' || name || ':' || sql FROM "
      "sqlite_master "
      "WHERE sql IS NOT NULL ORDER BY type, name;",
      -1, &o_stmt, NULL
  );
  if (sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  std::string out;
  while ((sqlRc = sqlite3_step (o_stmt)) == SQLITE_ROW) {
    out += reinterpret_cast<const char*> (
        sqlite3_column_text (o_stmt, 0)
    );
    out += '\n';
  }
  if (sqlRc != SQLITE_DONE) {
    const std::string errMsg = sqlite3_errmsg (db);
    sqlite3_finalize (o_stmt);
    return std::unexpected{make_sqlite3_err (sqlRc, errMsg)};
  }
  sqlite3_finalize (o_stmt);
  return out;
}

// NOTE: unnecessary usage of
// prepare - replace with sqlite3_exec
std::expected<int, Err> pragma_int (
    PileupDB& db, const char* pragmaSql
)
{
  sqlite3_stmt* o_stmt = NULL;
  int sqlRc =
      sqlite3_prepare_v2 (db, pragmaSql, -1, &o_stmt, NULL);
  if (sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }
  if (sqlRc = sqlite3_step (o_stmt); sqlRc != SQLITE_ROW) {
    const std::string errMsg = sqlite3_errmsg (db);
    sqlite3_finalize (o_stmt);
    return std::unexpected{make_sqlite3_err (sqlRc, errMsg)};
  }
  const int val = sqlite3_column_int (o_stmt, 0);
  sqlite3_finalize (o_stmt);
  return val;
}

// Confirm `db` has the same schema and connection-level pragmas as a
// freshly `init_db`-created database, so a loaded db is guaranteed to
// behave identically to one populated directly (demo/sam modes).
VoidOrErr verify_schema_and_pragmas (PileupDB& db)
{
  PileupDB refDb;
  if (auto r = init_db (refDb); !r) {
    return std::unexpected{r.error()};
  }

  auto refSchema = schema_fingerprint (refDb);
  if (!refSchema) {
    return std::unexpected{refSchema.error()};
  }
  auto loadedSchema = schema_fingerprint (db);
  if (!loadedSchema) {
    return std::unexpected{loadedSchema.error()};
  }
  if (*refSchema != *loadedSchema) {
    return std::unexpected{make_internal_err (
        "schema does not match the expected pileup-browser "
        "schema"
    )};
  }

  auto refFk = pragma_int (refDb, "PRAGMA foreign_keys;");
  if (!refFk) {
    return std::unexpected{refFk.error()};
  }
  auto loadedFk = pragma_int (db, "PRAGMA foreign_keys;");
  if (!loadedFk) {
    return std::unexpected{loadedFk.error()};
  }
  if (*refFk != *loadedFk) {
    return std::unexpected{make_internal_err (
        "foreign_keys pragma does not match the expected value"
    )};
  }

  auto refTempStore = pragma_int (refDb, "PRAGMA temp_store;");
  if (!refTempStore) {
    return std::unexpected{refTempStore.error()};
  }
  auto loadedTempStore = pragma_int (db, "PRAGMA temp_store;");
  if (!loadedTempStore) {
    return std::unexpected{loadedTempStore.error()};
  }
  if (*refTempStore != *loadedTempStore) {
    return std::unexpected{make_internal_err (
        "temp_store pragma does not match the expected value"
    )};
  }

  return {};
}

}  // namespace

VoidOrErr load_from_disk (PileupDB& db, const std::string& path)
{
  /*
    Copy a database file on disk into an in-memory PileupDB,
    via sqlite3's online backup API.
  */
  int sqlRc = SQLITE_OK;
  sqlite3* o_fileDb = NULL;
  sqlite3_backup* o_backup = NULL;

  if (sqlRc = sqlite3_open_v2 (
          path.c_str(), &o_fileDb, SQLITE_OPEN_READONLY, NULL
      );
      sqlRc != SQLITE_OK) {
    // NOTE: the error here belongs to o_fileDb (the handle
    // that failed to open), not db.
    goto err_open;
  }

  if (o_backup =
          sqlite3_backup_init (db, "main", o_fileDb, "main");
      o_backup == NULL) {
    sqlRc = sqlite3_errcode (db);
    goto err_sql;
  }

  // -1: copy all remaining pages in a single step.
  // ASSUMPTION: o_fileDb is quiescent (no concurrent writer
  // holding a lock) for the duration of the copy.
  if (sqlRc = sqlite3_backup_step (o_backup, -1);
      sqlRc != SQLITE_DONE) {
    sqlite3_backup_finish (o_backup);
    goto err_sql;
  }

  if (sqlRc = sqlite3_backup_finish (o_backup);
      sqlRc != SQLITE_OK) {
    goto err_sql;
  }
  o_backup = NULL;  // fin

  if (sqlRc = sqlite3_close_v2 (o_fileDb); sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errstr (sqlRc))
    };
  }
  o_fileDb = NULL;

  if (auto r = verify_schema_and_pragmas (db); !r) {
    Err err = r.error();
    err.msg = "loaded db from " + path +
              " failed verification: " + err.msg;
    return std::unexpected{err};
  }

  return {};

err_open: {
  const std::string errMsg = sqlite3_errmsg (o_fileDb);
  sqlite3_close_v2 (o_fileDb);
  return std::unexpected{make_sqlite3_err (sqlRc, errMsg)};
}

err_sql: {
  // NOTE: per sqlite3 docs, errors from backup_init/backup_step
  // are stored on the *destination* handle, so db (the in-memory
  // connection being loaded into) is the right handle to query
  // here in every failure case above.
  const std::string errMsg = sqlite3_errmsg (db);
  sqlite3_close_v2 (o_fileDb);
  return std::unexpected{make_sqlite3_err (sqlRc, errMsg)};
}
}

LocusOrErr get_locus_data (const PileupDB& db)
{
  LocusData out;

  sqlite3_stmt* o_stmt = NULL;
  int sqlRc = sqlite3_prepare_v2 (
      db, rsql_SelectLoci.data(),
      static_cast<int> (rsql_SelectLoci.size()), &o_stmt, NULL
  );
  if (sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  if (sqlRc = sqlite3_step (o_stmt); sqlRc != SQLITE_ROW) {
    const std::string errMsg = sqlite3_errmsg (db);
    sqlite3_finalize (o_stmt);
    return std::unexpected{make_sqlite3_err (sqlRc, errMsg)};
  }

  out.contig = {
      reinterpret_cast<const char*> (
          sqlite3_column_text (o_stmt, 0)
      ),
      static_cast<size_t> (sqlite3_column_bytes (o_stmt, 0))
  };

  out.pos = sqlite3_column_int64 (o_stmt, 1);

  out.start = sqlite3_column_int64 (
      o_stmt, 2
  );  // can be NULL, should check (TODO) - or make not nullable
  out.end = sqlite3_column_int64 (o_stmt, 3);

  if (sqlite3_column_type (o_stmt, 4) != SQLITE_NULL) {
    out.refSlice.emplace (
        std::string{
            reinterpret_cast<const char*> (
                sqlite3_column_text (o_stmt, 4)
            ),
            static_cast<size_t> (
                sqlite3_column_bytes (o_stmt, 4)
            )
        }
    );
  }

  sqlite3_finalize (o_stmt);
  return out;
}

LocusData make_locus_data (
    std::string contigName, hts_pos_t pos,
    const GenomicSpan& span, std::optional<std::string> refSlice
)
{
  return LocusData{
      .contig = std::move (contigName),
      .pos = pos,
      .start = span.start,
      .end = span.end,
      .refSlice = std::move (refSlice)
  };
}

namespace {

std::expected<SqliteStmt, Err> prepare_insert_metadata_stmt (
    PileupDB& db
)
{
  SqliteStmt stmt;
  int rc;
  if (rc = sqlite3_prepare_v2 (
          db, rsql_InsertMetadata.data(),
          static_cast<int> (rsql_InsertMetadata.size()),
          &stmt.o_ptr, NULL
      );
      rc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errmsg (db))
    };
  }
  return stmt;
}

}  // namespace

VoidOrErr insert_metadata (PileupDB& db, const AlnFile& _)
{
  /*
    insert provenance metadata into database.

    uses automatic transaction handling.
  */
  // NOTE: automatic transaction handling
  // means no need to handle rollback on
  // error paths.
  auto r = prepare_insert_metadata_stmt (db);
  if (!r) {
    return std::unexpected{r.error()};
  }
  auto stmt{std::move (*r)};

  const int col = 1;
  int rc;
  // placeholder bind;
  // in future will use data from AlnFile
  if (rc = sqlite3_bind_int (stmt, col, 1); rc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errstr (rc))
    };
  }
  if (rc = sqlite3_step (stmt); rc != SQLITE_DONE) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errmsg (db))
    };
  }
  return {};
}

VoidOrErr insert_pileup (
    PileupDB& db, const AlnFile& aln, const PileupPosition& pos,
    const std::optional<FastaFile>& ff
)
{
  auto ppRet = prepare_pileup (aln, pos);
  if (!ppRet) {
    return std::unexpected{ppRet.error()};
  }
  auto reads{std::move (*ppRet)};

  auto contigName = sam_hdr_tid2name (aln.o_hdr, pos.tid);

  // With no covering reads there's no meaningful span to fetch a
  // reference slice for; fall back to a zero-width span at the pileup
  // position itself rather than get_pileup_span's unpopulated sentinel
  // (INT64_MAX/0), which would otherwise pass an inverted range to
  // fetch_region and fail the whole insert.
  GenomicSpan rSpan{pos.pos, pos.pos};
  std::optional<std::string> refSlice;
  if (reads.nPlp > 0) {
    rSpan = get_pileup_span (reads);
    PLOGD << fmt::format (
        "Pileup spans {}-{}", rSpan.start, rSpan.end
    );
    if (ff) {
      auto regRet =
          fetch_region (*ff, contigName, rSpan.start, rSpan.end);
      if (!regRet) {
        return std::unexpected (regRet.error());
      }
      refSlice = *regRet;
    }
  }

  // NOTE: not checking if loci already exists
  // NOTE: if insert_reads_interal fails,
  // insert_loci not rolled back.
  // NOTE: nreads not currently recorded
  // in loci table
  auto ilRet = insert_loci (
      db, make_locus_data (contigName, pos.pos, rSpan, refSlice)
  );
  if (!ilRet) {
    return std::unexpected{ilRet.error()};
  }
  auto lociId = *ilRet;

  auto tid2str = [&aln] (int tid) {
    return sam_hdr_tid2name (aln.o_hdr, tid);
  };
  auto irRet = insert_reads_internal (
      db, reads.plpArr, reads.nPlp, lociId, tid2str
  );
  if (!irRet) {
    return std::unexpected{irRet.error()};
  }
  return {};
};

BoolOrErr next_read (sqlite3_stmt* stmt, const PileupDB& db)
{
  const int rc = sqlite3_step (stmt);
  if (rc == SQLITE_DONE) {
    return false;
  }
  if (rc != SQLITE_ROW) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errmsg (db))
    };
  }
  return true;
}
