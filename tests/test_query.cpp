#include <htslib/sam.h>
#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "backend/PileupDB.hpp"
#include "backend/pileup_ingest.hpp"
#include "shared/err.hpp"

namespace {

// Seeds rows with distinct flag/mapq/rstart so filter/order tests can
// distinguish them.
struct SeededDb {
  PileupDB db;
  int lociId;
};

SeededDb make_seeded_db()
{
  PileupDB db;
  REQUIRE (init_db (db));
  auto lociIdRet = insert_loci (
      db, LocusData{
              .contig = "chr1",
              .pos = 100,
              .start = 100,
              .end = 200,
              .refSlice = std::nullopt
          }
  );
  REQUIRE (lociIdRet);
  const int lociId = *lociIdRet;

  auto stmtRet = prepare_insert_reads_stmt (db);
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};

  auto row = [] (std::string qName, uint16_t flag, uint8_t mapQ,
                 hts_pos_t rstart) {
    PileupFields pf{};
    pf.qName = std::move (qName);
    pf.flag = flag;
    pf.start = rstart;
    pf.end = rstart + 50;
    pf.mapQ = mapQ;
    pf.base = 'A';
    pf.baseQual = 30;
    pf.qPos = 0;
    pf.indel = 0;
    pf.isDel = false;
    pf.isHead = false;
    pf.isTail = false;
    pf.isRefSkip = false;
    pf.cig = "50M";
    pf.seqBases = std::string (50, 'A');
    pf.qualAscii = std::string (50, 'I');
    pf.mtidName = "";
    pf.mStart = -1;
    pf.auxJson = "";
    pf.rawCig = {
        static_cast<uint32_t> (bam_cigar_gen (50, BAM_CMATCH))
    };
    pf.nCig = pf.rawCig.size();
    return pf;
  };

  const std::vector<PileupFields> rows{
      row ("readA", 99, 60, 100),
      row ("readB", 4, 10, 120),
      row ("readC", 99, 45, 90),
  };
  for (const auto& pf : rows) {
    REQUIRE (bind_pileup_fields (stmt, lociId, pf) == SQLITE_OK);
    REQUIRE (sqlite3_step (stmt) == SQLITE_DONE);
    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
  }

  return SeededDb{.db = std::move (db), .lociId = lociId};
}

size_t count_rows (sqlite3_stmt* o_stmt, PileupDB& db)
{
  size_t n = 0;
  while (true) {
    auto r = next_read (o_stmt, db);
    REQUIRE (r);
    if (!*r) {
      break;
    }
    ++n;
  }
  return n;
}

}  // namespace

TEST_CASE (
    "prepare_select_reads with no fragments returns every row",
    "[query]"
)
{
  auto seeded = make_seeded_db();

  auto stmtRet =
      prepare_select_reads (seeded.db, DynamicFragments{});
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};
  CHECK (count_rows (stmt, seeded.db) == 3);
}

TEST_CASE (
    "prepare_select_reads filters on a WHERE fragment", "[query]"
)
{
  auto seeded = make_seeded_db();

  DynamicFragments frags{.where = {"flag = 99"}, .orderBy = ""};
  auto stmtRet = prepare_select_reads (seeded.db, frags);
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};
  CHECK (count_rows (stmt, seeded.db) == 2);
}

TEST_CASE (
    "prepare_select_reads joins WHERE fragments with a bare "
    "space -- "
    "the caller supplies the boolean connective",
    "[query]"
)
{
  auto seeded = make_seeded_db();

  DynamicFragments frags{
      .where = {"flag = 99", "AND mapq > 50"}, .orderBy = ""
  };
  auto stmtRet = prepare_select_reads (seeded.db, frags);
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};
  CHECK (
      count_rows (stmt, seeded.db) == 1
  );  // only readA (flag 99, mapq 60)
}

TEST_CASE ("prepare_select_reads honours ORDER BY", "[query]")
{
  auto seeded = make_seeded_db();

  DynamicFragments frags{.where = {}, .orderBy = "rstart DESC"};
  auto stmtRet = prepare_select_reads (seeded.db, frags);
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};

  auto r1 = next_read (stmt, seeded.db);
  REQUIRE (r1);
  REQUIRE (*r1);
  CHECK (get_rstart (stmt) == 120);  // readB

  auto r2 = next_read (stmt, seeded.db);
  REQUIRE (r2);
  REQUIRE (*r2);
  CHECK (get_rstart (stmt) == 100);  // readA

  auto r3 = next_read (stmt, seeded.db);
  REQUIRE (r3);
  REQUIRE (*r3);
  CHECK (get_rstart (stmt) == 90);  // readC
}

TEST_CASE (
    "prepare_select_reads surfaces an sqlite3 error on a "
    "malformed "
    "fragment",
    "[query]"
)
{
  auto seeded = make_seeded_db();

  DynamicFragments frags{.where = {"flag ="}, .orderBy = ""};
  auto stmtRet = prepare_select_reads (seeded.db, frags);
  REQUIRE_FALSE (stmtRet);
  CHECK (stmtRet.error().src == ErrSrc::sqlite3);
}

TEST_CASE (
    "a stacked-query WHERE fragment only compiles up to the "
    "first ';' "
    "-- trailing SQL is inert, not executed",
    "[query]"
)
{
  auto seeded = make_seeded_db();

  DynamicFragments frags{
      .where = {"1=1; DELETE FROM reads"}, .orderBy = ""
  };
  auto stmtRet = prepare_select_reads (seeded.db, frags);
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};
  CHECK (sqlite3_stmt_readonly (stmt));
  CHECK (count_rows (stmt, seeded.db) == 3);  // DELETE never ran

  // Confirm directly against the table too.
  sqlite3_stmt* o_check = NULL;
  const std::string_view sql = "SELECT COUNT(*) FROM reads;";
  REQUIRE (
      sqlite3_prepare_v2 (
          seeded.db, sql.data(), static_cast<int> (sql.size()),
          &o_check, NULL
      ) == SQLITE_OK
  );
  REQUIRE (sqlite3_step (o_check) == SQLITE_ROW);
  CHECK (sqlite3_column_int64 (o_check, 0) == 3);
  sqlite3_finalize (o_check);
}

TEST_CASE (
    "prepare_count_reads mirrors filtered/unfiltered counts",
    "[query]"
)
{
  auto seeded = make_seeded_db();

  SECTION ("no filter")
  {
    auto stmtRet = prepare_count_reads (seeded.db, {});
    REQUIRE (stmtRet);
    auto stmt{std::move (*stmtRet)};
    REQUIRE (sqlite3_step (stmt) == SQLITE_ROW);
    CHECK (sqlite3_column_int64 (stmt, 0) == 3);
  }

  SECTION ("with filter")
  {
    auto stmtRet =
        prepare_count_reads (seeded.db, {"flag = 99"});
    REQUIRE (stmtRet);
    auto stmt{std::move (*stmtRet)};
    REQUIRE (sqlite3_step (stmt) == SQLITE_ROW);
    CHECK (sqlite3_column_int64 (stmt, 0) == 2);
  }

  SECTION ("malformed filter surfaces a sqlite3 error")
  {
    auto stmtRet = prepare_count_reads (seeded.db, {"flag ="});
    REQUIRE_FALSE (stmtRet);
    CHECK (stmtRet.error().src == ErrSrc::sqlite3);
  }
}
