#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>

#include "backend/PileupDB.hpp"
#include "backend/hts_types.hpp"
#include "backend/pileup_ingest.hpp"
#include "shared/err.hpp"

namespace {

// One row per (name, expected type) in sqlite_master, for schema checks.
bool object_exists (
    PileupDB& db, const std::string& type,
    const std::string& name
)
{
  sqlite3_stmt* o_stmt = NULL;
  const std::string_view sql =
      "SELECT 1 FROM sqlite_master WHERE type = ? AND name = ?;";
  REQUIRE (
      sqlite3_prepare_v2 (
          db, sql.data(), static_cast<int> (sql.size()), &o_stmt,
          NULL
      ) == SQLITE_OK
  );
  sqlite3_bind_text (
      o_stmt, 1, type.c_str(), -1, SQLITE_TRANSIENT
  );
  sqlite3_bind_text (
      o_stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT
  );
  const bool found = sqlite3_step (o_stmt) == SQLITE_ROW;
  sqlite3_finalize (o_stmt);
  return found;
}

int64_t pragma_value (PileupDB& db, const std::string& pragma)
{
  sqlite3_stmt* o_stmt = NULL;
  const std::string sql = "PRAGMA " + pragma + ";";
  REQUIRE (
      sqlite3_prepare_v2 (db, sql.c_str(), -1, &o_stmt, NULL) ==
      SQLITE_OK
  );
  REQUIRE (sqlite3_step (o_stmt) == SQLITE_ROW);
  const int64_t val = sqlite3_column_int64 (o_stmt, 0);
  sqlite3_finalize (o_stmt);
  return val;
}

}  // namespace

TEST_CASE (
    "init_db creates the expected schema and pragmas", "[schema]"
)
{
  PileupDB db;
  REQUIRE (init_db (db));

  CHECK (object_exists (db, "table", "metadata"));
  CHECK (object_exists (db, "table", "loci"));
  CHECK (object_exists (db, "table", "reads"));
  CHECK (object_exists (db, "index", "idx_reads_loci_id"));

  CHECK (pragma_value (db, "foreign_keys") == 1);
  CHECK (pragma_value (db, "temp_store") == 2);  // 2 == MEMORY
}

TEST_CASE (
    "insert_metadata inserts exactly one provenance row",
    "[schema]"
)
{
  PileupDB db;
  REQUIRE (init_db (db));

  REQUIRE (insert_metadata (db, AlnFile{}));

  sqlite3_stmt* o_stmt = NULL;
  const std::string_view sql = "SELECT COUNT(*) FROM metadata;";
  REQUIRE (
      sqlite3_prepare_v2 (
          db, sql.data(), static_cast<int> (sql.size()), &o_stmt,
          NULL
      ) == SQLITE_OK
  );
  REQUIRE (sqlite3_step (o_stmt) == SQLITE_ROW);
  CHECK (sqlite3_column_int64 (o_stmt, 0) == 1);
  sqlite3_finalize (o_stmt);
}

TEST_CASE ("insert_loci / get_locus_data round trip", "[schema]")
{
  PileupDB db;
  REQUIRE (init_db (db));

  SECTION ("with a reference slice")
  {
    LocusData locus{
        .contig = "chr1",
        .pos = 12345,
        .start = 12300,
        .end = 12400,
        .refSlice = std::make_optional<std::string> ("ACGTACGT")
    };
    auto idRet = insert_loci (db, locus);
    REQUIRE (idRet);
    CHECK (*idRet > 0);

    auto readBack = get_locus_data (db);
    REQUIRE (readBack);
    CHECK (readBack->contig == "chr1");
    CHECK (readBack->pos == 12345);
    CHECK (readBack->start == 12300);
    CHECK (readBack->end == 12400);
    REQUIRE (readBack->refSlice.has_value());
    CHECK (*readBack->refSlice == "ACGTACGT");
  }

  SECTION ("with no reference slice")
  {
    LocusData locus{
        .contig = "chr2",
        .pos = 500,
        .start = 400,
        .end = 600,
        .refSlice = std::nullopt
    };
    auto idRet = insert_loci (db, locus);
    REQUIRE (idRet);

    auto readBack = get_locus_data (db);
    REQUIRE (readBack);
    CHECK (readBack->contig == "chr2");
    CHECK_FALSE (readBack->refSlice.has_value());
  }
}

TEST_CASE (
    "get_locus_data on an empty loci table surfaces a sqlite3 "
    "error",
    "[schema]"
)
{
  PileupDB db;
  REQUIRE (init_db (db));

  auto readBack = get_locus_data (db);
  REQUIRE_FALSE (readBack);
  CHECK (readBack.error().src == ErrSrc::sqlite3);
}

TEST_CASE (
    "make_locus_data is a pure value constructor", "[schema]"
)
{
  GenomicSpan span{.start = 100, .end = 250};
  auto locus = make_locus_data (
      "chr3", 175, span, std::make_optional<std::string> ("TTTT")
  );

  CHECK (locus.contig == "chr3");
  CHECK (locus.pos == 175);
  CHECK (locus.start == 100);
  CHECK (locus.end == 250);
  REQUIRE (locus.refSlice.has_value());
  CHECK (*locus.refSlice == "TTTT");
}
