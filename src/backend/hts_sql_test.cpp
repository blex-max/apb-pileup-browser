#include "backend/hts_sql.hpp"

#include <htslib/sam.h>
#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "backend/schema.hpp"
#include "doctest.h"

/* ---- stringify_cigar ---- */

TEST_CASE ("stringify_cigar renders CIGAR arrays as text")
{
  auto gen = [] (uint32_t len, uint32_t op) {
    return static_cast<uint32_t> (bam_cigar_gen (len, op));
  };

  SUBCASE ("single op")
  {
    std::vector<uint32_t> cig{gen (50, BAM_CMATCH)};
    CHECK (hts2sql::stringify_cigar (cig.data(), cig.size()) == "50M");
  }

  SUBCASE ("multi op")
  {
    std::vector<uint32_t> cig{
        gen (10, BAM_CSOFT_CLIP), gen (40, BAM_CMATCH), gen (5, BAM_CINS),
        gen (2, BAM_CDEL)
    };
    CHECK (
        hts2sql::stringify_cigar (cig.data(), cig.size()) == "10S40M5I2D"
    );
  }

  SUBCASE ("zero ops")
  {
    CHECK (hts2sql::stringify_cigar (nullptr, 0) == "");
  }
}

/* ---- append_json_escaped ---- */

TEST_CASE ("append_json_escaped escapes per JSON string rules")
{
  SUBCASE ("quote and backslash")
  {
    std::string out;
    const std::string in = "A\"B";
    hts2sql::append_json_escaped (in.data(), in.size(), out);
    CHECK (out == "A\\\"B");

    out.clear();
    const std::string in2 = "A\\B";
    hts2sql::append_json_escaped (in2.data(), in2.size(), out);
    CHECK (out == "A\\\\B");
  }

  SUBCASE ("control characters escape to \\u00XX")
  {
    std::string out;
    const char in[] = {static_cast<char> (0x01)};
    hts2sql::append_json_escaped (in, 1, out);
    CHECK (out == "\\u0001");

    out.clear();
    const char in2[] = {static_cast<char> (0x1f)};
    hts2sql::append_json_escaped (in2, 1, out);
    CHECK (out == "\\u001f");
  }

  SUBCASE (
      "printable ASCII and non-ASCII bytes pass through "
      "unchanged"
  )
  {
    std::string out;
    const std::string in = "hello";
    hts2sql::append_json_escaped (in.data(), in.size(), out);
    CHECK (out == "hello");

    out.clear();
    const char in2[] = {static_cast<char> (0xC3)};
    hts2sql::append_json_escaped (in2, 1, out);
    REQUIRE (out.size() == 1);
    CHECK (static_cast<unsigned char> (out[0]) == 0xC3);
  }

  SUBCASE ("empty input appends nothing")
  {
    std::string out = "unchanged";
    hts2sql::append_json_escaped (nullptr, 0, out);
    CHECK (out == "unchanged");
  }
}

/* ---- aux1_to_json ----
 * Raw buffers hand-built per htslib's on-wire aux tag encoding: 2-byte
 * tag name, 1-byte type, then type-specific value bytes. aux1Start points
 * at the type byte (2 bytes past the buffer start); aux1End bounds the
 * whole buffer. */

TEST_CASE ("aux1_to_json converts scalar/char/string tags")
{
  SUBCASE ("numeric scalar (type 'c', int8)")
  {
    uint8_t buf[] = {'N', 'M', 'c', 5};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"NM\":5");
  }

  SUBCASE ("char (type 'A')")
  {
    uint8_t buf[] = {'X', 'A', 'A', 'Q'};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XA\":\"Q\"");
  }

  SUBCASE ("string (type 'Z')")
  {
    uint8_t buf[] = {'R', 'G', 'Z', 's', 'a', 'm',
                     'p', 'l', 'e', '1', '\0'};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"RG\":\"sample1\"");
  }

  SUBCASE ("string value containing '\"' and '\\' gets escaped")
  {
    uint8_t buf[] = {'Z', 'Z', 'Z', 'a', '"', 'b', '\\', 'c', '\0'};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);

    std::string expectedVal = "a";
    expectedVal += '\\';
    expectedVal += '"';
    expectedVal += "b";
    expectedVal += '\\';
    expectedVal += '\\';
    expectedVal += "c";
    std::string expected = "\"ZZ\":\"";
    expected += expectedVal;
    expected += "\"";
    CHECK (*r == expected);
  }
}

TEST_CASE (
    "aux1_to_json handles zero-, one-, and multi-element 'B' "
    "arrays"
)
{
  // Regression coverage for the 'B' branch: htslib only emits a
  // leading ',' once there's at least one element, so the zero-element
  // case must short-circuit rather than read past the formatted
  // buffer, and neither case should leave a trailing comma before ']'.
  SUBCASE ("zero elements")
  {
    uint8_t buf[] = {'X', 'Y', 'B', 'c', 0, 0, 0, 0};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XY\":[]");
  }

  SUBCASE ("single element")
  {
    uint8_t buf[] = {'X', 'Y', 'B', 'c', 1, 0, 0, 0, 5};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XY\":[5]");
  }

  SUBCASE ("three elements")
  {
    uint8_t buf[] = {'X', 'Y', 'B', 'c', 3, 0, 0, 0, 1, 2, 3};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XY\":[1,2,3]");
  }
}

/* ---- conversion boundary / CHECK(json_valid(tags)) ----
 * metadata is deliberately not inserted for these cases: an empty
 * metadata table means validate_read_span has nothing to join against
 * and never fires (see schema.hpp), which decouples these fixtures from
 * needing locus-consistent start/end -- trigger correctness is covered
 * separately in schema_test.cpp. */

namespace {

hts2sql::PileupFields make_basic_fields (hts_pos_t mStart = -1)
{
  hts2sql::PileupFields pf{};
  pf.qName = "read0";
  pf.flag = 99;
  pf.start = 99;  // 0-indexed input -> db start == 100
  pf.end = 170;  // unshifted -> db end == 170
  pf.mapQ = 60;
  pf.base = 'A';
  pf.baseQual = 37;
  pf.qPos = 4;  // 0-indexed input -> db qpos == 5
  pf.indel = 0;
  pf.isDel = false;
  pf.isHead = false;
  pf.isTail = false;
  pf.isRefSkip = false;
  pf.cig = "50M";
  pf.seqBases = "ACGTACGTAC";
  pf.qualAscii = "IIIIIIIIII";
  pf.mtidName = "";
  pf.mStart = mStart;
  pf.auxJson = "";
  pf.rawCig = {static_cast<uint32_t> (bam_cigar_gen (50, BAM_CMATCH))};
  pf.nCig = pf.rawCig.size();
  return pf;
}

}  // namespace

TEST_CASE (
    "insert_metadata converts 0-indexed input to 1-indexed locus "
    "data"
)
{
  PileupDB db = PileupDB::init();

  SUBCASE ("with a reference slice")
  {
    REQUIRE (
        hts2sql::insert_metadata (
            db, "chr1", 99, GenomicSpan{.start = 99, .end = 200},
            std::make_optional<std::string> ("ACGTACGT")
        ) == SQLITE_OK
    );

    auto locus = query::get_locus_data (db);
    CHECK (locus.contig == "chr1");
    CHECK (locus.pos == 100);
    CHECK (locus.start == 100);
    CHECK (locus.end == 200);
    REQUIRE (locus.refSlice.has_value());
    CHECK (*locus.refSlice == "ACGTACGT");
  }

  SUBCASE ("with no reference slice")
  {
    REQUIRE (
        hts2sql::insert_metadata (
            db, "chr2", 49, GenomicSpan{.start = 39, .end = 60},
            std::nullopt
        ) == SQLITE_OK
    );

    auto locus = query::get_locus_data (db);
    CHECK (locus.contig == "chr2");
    CHECK (locus.pos == 50);
    CHECK (locus.start == 40);
    CHECK (locus.end == 60);
    CHECK_FALSE (locus.refSlice.has_value());
  }
}

TEST_CASE (
    "bind_pileup_fields converts 0-indexed input to 1-indexed row "
    "data"
)
{
  PileupDB db = PileupDB::init();

  SUBCASE ("mStart >= 0 shifts to a 1-indexed mstart")
  {
    auto stmt = hts2sql::prepare_insert_reads_stmt (db);
    auto pf = make_basic_fields (49);
    hts2sql::bind_pileup_fields (stmt, pf);
    REQUIRE (sqlite3_step (stmt) == SQLITE_DONE);

    sqlite3_stmt* o_stmt = NULL;
    const std::string_view sql = "SELECT * FROM reads;";
    REQUIRE (
        sqlite3_prepare_v2 (
            db, sql.data(), static_cast<int> (sql.size()), &o_stmt, NULL
        ) == SQLITE_OK
    );
    REQUIRE (sqlite3_step (o_stmt) == SQLITE_ROW);

    CHECK (
        sqlite3_column_int64 (o_stmt, schema::ReadTableSelect::start) ==
        pf.start + 1
    );
    CHECK (
        sqlite3_column_int64 (o_stmt, schema::ReadTableSelect::end) ==
        pf.end
    );
    CHECK (
        sqlite3_column_int64 (o_stmt, schema::ReadTableSelect::qpos) ==
        pf.qPos + 1
    );
    CHECK (
        sqlite3_column_int64 (o_stmt, schema::ReadTableSelect::mstart) ==
        pf.mStart + 1
    );
    sqlite3_finalize (o_stmt);
  }

  SUBCASE ("mStart < 0 stays NULL rather than shifting")
  {
    auto stmt = hts2sql::prepare_insert_reads_stmt (db);
    auto pf = make_basic_fields (-1);
    hts2sql::bind_pileup_fields (stmt, pf);
    REQUIRE (sqlite3_step (stmt) == SQLITE_DONE);

    sqlite3_stmt* o_stmt = NULL;
    const std::string_view sql = "SELECT * FROM reads;";
    REQUIRE (
        sqlite3_prepare_v2 (
            db, sql.data(), static_cast<int> (sql.size()), &o_stmt, NULL
        ) == SQLITE_OK
    );
    REQUIRE (sqlite3_step (o_stmt) == SQLITE_ROW);

    CHECK (
        sqlite3_column_type (o_stmt, schema::ReadTableSelect::mstart) ==
        SQLITE_NULL
    );
    sqlite3_finalize (o_stmt);
  }
}

TEST_CASE (
    "CHECK(json_valid(tags)) accepts valid JSON and rejects "
    "malformed JSON"
)
{
  PileupDB db = PileupDB::init();

  SUBCASE ("valid JSON is accepted")
  {
    auto stmt = hts2sql::prepare_insert_reads_stmt (db);
    auto pf = make_basic_fields();
    pf.auxJson = "{\"XY\":[1,2,3]}";
    hts2sql::bind_pileup_fields (stmt, pf);
    CHECK (sqlite3_step (stmt) == SQLITE_DONE);
  }

  SUBCASE (
      "deliberately invalid JSON is rejected by the CHECK "
      "constraint"
  )
  {
    auto stmt = hts2sql::prepare_insert_reads_stmt (db);
    auto pf = make_basic_fields();
    pf.auxJson = "{\"XY\":[1,2,3,]}";  // trailing comma -- malformed
    hts2sql::bind_pileup_fields (stmt, pf);
    const int rc = sqlite3_step (stmt);
    CHECK ((rc & 0xFF) == SQLITE_CONSTRAINT);
  }
}

/* ---- build_where_clause / prepare_select_reads ---- */

namespace {

// Seeded so validate_read_span passes for every row (locus pos=150,
// start=100, end=200) and start values are distinct, for the ORDER BY
// case. Goes through the real hts2sql:: insert API, unlike
// schema_test.cpp's raw-SQL helpers, since these tests exercise query::
// functions that only make sense against a realistic table.
struct SeededDb {
  PileupDB db;
};

SeededDb make_seeded_db()
{
  PileupDB db = PileupDB::init();
  REQUIRE (
      hts2sql::insert_metadata (
          db, "chr1", 149, GenomicSpan{.start = 99, .end = 200},
          std::nullopt
      ) == SQLITE_OK
  );

  auto stmt = hts2sql::prepare_insert_reads_stmt (db);

  auto row = [] (std::string qName, uint16_t flag, uint8_t mapQ,
                 hts_pos_t start, hts_pos_t end) {
    hts2sql::PileupFields pf{};
    pf.qName = std::move (qName);
    pf.flag = flag;
    pf.start = start - 1;  // 0-indexed input -> db start
    pf.end = end;  // unshifted -> db end
    pf.mapQ = mapQ;
    pf.base = 'A';
    pf.baseQual = 30;
    pf.qPos = 0;
    pf.indel = 0;
    pf.isDel = false;
    pf.isHead = false;
    pf.isTail = false;
    pf.isRefSkip = false;
    pf.cig = "1M";
    pf.seqBases = "A";
    pf.qualAscii = "I";
    pf.mtidName = "";
    pf.mStart = -1;
    pf.auxJson = "";
    pf.rawCig = {static_cast<uint32_t> (bam_cigar_gen (1, BAM_CMATCH))};
    pf.nCig = pf.rawCig.size();
    return pf;
  };

  // (qName, flag, mapQ, start, end) -- all satisfy
  // 100 <= start <= 150 <= end <= 200.
  const std::vector<hts2sql::PileupFields> rows{
      row ("readA", 99, 60, 100, 170),
      row ("readB", 4, 10, 120, 190),
      row ("readC", 99, 45, 140, 200),
  };
  for (const auto& pf : rows) {
    hts2sql::bind_pileup_fields (stmt, pf);
    REQUIRE (sqlite3_step (stmt) == SQLITE_DONE);
    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
  }

  return SeededDb{.db = std::move (db)};
}

}  // namespace

TEST_CASE ("build_where_clause left-wraps fragments in parens")
{
  CHECK (query::build_where_clause ({}) == "");
  CHECK (query::build_where_clause ({"flag = 99"}) == "flag = 99");

  // Two fragments can't distinguish paren-wrapping from a bare-space
  // join -- three fragments where precedence matters can.
  CHECK (
      query::build_where_clause ({"a", "OR b", "AND c"}) ==
      "((a) OR b) AND c"
  );
}

TEST_CASE ("prepare_select_reads with no fragments returns every row")
{
  auto seeded = make_seeded_db();

  auto stmtRet = query::prepare_select_reads (
      seeded.db, schema::ReadTableSelect::sqlPrefix, {}
  );
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};
  auto countRet = query::count_rows (stmt);
  REQUIRE (countRet);
  CHECK (*countRet == 3);
}

TEST_CASE ("prepare_select_reads filters on WHERE fragments")
{
  auto seeded = make_seeded_db();

  SUBCASE ("single fragment")
  {
    query::DynamicFragments frags{.where = {"flag = 99"}, .orderBy = ""};
    auto stmtRet = query::prepare_select_reads (
        seeded.db, schema::ReadTableSelect::sqlPrefix, frags
    );
    REQUIRE (stmtRet);
    auto stmt{std::move (*stmtRet)};
    auto countRet = query::count_rows (stmt);
    REQUIRE (countRet);
    CHECK (*countRet == 2);  // readA, readC
  }

  SUBCASE ("multiple fragments")
  {
    query::DynamicFragments frags{
        .where = {"flag = 99", "AND mapq > 50"}, .orderBy = ""
    };
    auto stmtRet = query::prepare_select_reads (
        seeded.db, schema::ReadTableSelect::sqlPrefix, frags
    );
    REQUIRE (stmtRet);
    auto stmt{std::move (*stmtRet)};
    auto countRet = query::count_rows (stmt);
    REQUIRE (countRet);
    CHECK (*countRet == 1);  // readA only (flag 99, mapq 60)
  }
}

TEST_CASE ("prepare_select_reads honours ORDER BY")
{
  auto seeded = make_seeded_db();

  query::DynamicFragments frags{.where = {}, .orderBy = "start DESC"};
  auto stmtRet = query::prepare_select_reads (
      seeded.db, schema::ReadTableSelect::sqlPrefix, frags
  );
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};

  auto r1 = query::next_read (stmt);
  REQUIRE (r1);
  REQUIRE (*r1 == query::RowIterStatus::rowAvail);
  CHECK (
      sqlite3_column_int64 (stmt, schema::ReadTableSelect::start) == 140
  );  // readC

  auto r2 = query::next_read (stmt);
  REQUIRE (r2);
  REQUIRE (*r2 == query::RowIterStatus::rowAvail);
  CHECK (
      sqlite3_column_int64 (stmt, schema::ReadTableSelect::start) == 120
  );  // readB

  auto r3 = query::next_read (stmt);
  REQUIRE (r3);
  REQUIRE (*r3 == query::RowIterStatus::rowAvail);
  CHECK (
      sqlite3_column_int64 (stmt, schema::ReadTableSelect::start) == 100
  );  // readA
}

TEST_CASE (
    "prepare_select_reads surfaces an sqlite3 error on a malformed "
    "fragment"
)
{
  auto seeded = make_seeded_db();

  query::DynamicFragments frags{.where = {"flag ="}, .orderBy = ""};
  auto stmtRet = query::prepare_select_reads (
      seeded.db, schema::ReadTableSelect::sqlPrefix, frags
  );
  REQUIRE_FALSE (stmtRet);
}

TEST_CASE (
    "prepare_select_reads rejects a non-SELECT prefix via the "
    "readonly guard"
)
{
  auto seeded = make_seeded_db();

  auto stmtRet =
      query::prepare_select_reads (seeded.db, "DELETE FROM reads", {});
  REQUIRE_FALSE (stmtRet);
  CHECK (stmtRet.error() == SQLITE_READONLY);
}

TEST_CASE (
    "a stacked-query WHERE fragment only compiles up to the first "
    "';' -- trailing SQL is inert, not executed"
)
{
  auto seeded = make_seeded_db();

  query::DynamicFragments frags{
      .where = {"1=1; DELETE FROM reads"}, .orderBy = ""
  };
  auto stmtRet = query::prepare_select_reads (
      seeded.db, schema::ReadTableSelect::sqlPrefix, frags
  );
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};
  CHECK (sqlite3_stmt_readonly (stmt));
  auto countRet = query::count_rows (stmt);
  REQUIRE (countRet);
  CHECK (*countRet == 3);  // DELETE never ran

  // Confirm directly against the table too.
  sqlite3_stmt* o_check = NULL;
  const std::string_view sql = "SELECT COUNT(*) FROM reads;";
  REQUIRE (
      sqlite3_prepare_v2 (
          seeded.db, sql.data(), static_cast<int> (sql.size()), &o_check,
          NULL
      ) == SQLITE_OK
  );
  REQUIRE (sqlite3_step (o_check) == SQLITE_ROW);
  CHECK (sqlite3_column_int64 (o_check, 0) == 3);
  sqlite3_finalize (o_check);
}
