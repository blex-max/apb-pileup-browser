#include <htslib/sam.h>
#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <string>

#include "backend/PileupDB.hpp"
#include "backend/pileup_ingest.hpp"

namespace {

PileupFields make_basic_fields (std::string qName = "read0")
{
  PileupFields pf{};
  pf.qName = std::move (qName);
  pf.flag = 99;
  pf.start = 100;
  pf.end = 150;
  pf.mapQ = 60;
  pf.base = 'A';
  pf.baseQual = 37;
  pf.qPos = 5;
  pf.indel = 0;
  pf.isDel = false;
  pf.isHead = false;
  pf.isTail = false;
  pf.isRefSkip = false;
  pf.cig = "50M";
  pf.seqBases = "ACGTACGTAC";
  pf.qualAscii = "IIIIIIIIII";
  pf.mtidName = "";
  pf.mStart = -1;
  pf.auxJson = "";
  pf.rawCig = {
      static_cast<uint32_t> (bam_cigar_gen (50, BAM_CMATCH))
  };
  pf.nCig = pf.rawCig.size();
  return pf;
}

}  // namespace

/* ---- stringify_cigar ---- */

TEST_CASE (
    "stringify_cigar renders CIGAR arrays as text", "[ingest]"
)
{
  auto gen = [] (uint32_t len, uint32_t op) {
    return static_cast<uint32_t> (bam_cigar_gen (len, op));
  };

  SECTION ("single op")
  {
    std::vector<uint32_t> cig{gen (50, BAM_CMATCH)};
    CHECK (stringify_cigar (cig.data(), cig.size()) == "50M");
  }

  SECTION ("multi op")
  {
    std::vector<uint32_t> cig{
        gen (10, BAM_CSOFT_CLIP), gen (40, BAM_CMATCH),
        gen (5, BAM_CINS), gen (2, BAM_CDEL)
    };
    CHECK (
        stringify_cigar (cig.data(), cig.size()) == "10S40M5I2D"
    );
  }

  SECTION ("zero ops")
  {
    CHECK (stringify_cigar (nullptr, 0) == "");
  }
}

/* ---- append_json_escaped ---- */

TEST_CASE (
    "append_json_escaped escapes per JSON string rules",
    "[ingest]"
)
{
  SECTION ("quote and backslash")
  {
    std::string out;
    const std::string in = "A\"B";
    append_json_escaped (in.data(), in.size(), out);
    CHECK (out == "A\\\"B");

    out.clear();
    const std::string in2 = "A\\B";
    append_json_escaped (in2.data(), in2.size(), out);
    CHECK (out == "A\\\\B");
  }

  SECTION ("control characters escape to \\u00XX")
  {
    std::string out;
    const char in[] = {static_cast<char> (0x01)};
    append_json_escaped (in, 1, out);
    CHECK (out == "\\u0001");

    out.clear();
    const char in2[] = {static_cast<char> (0x1f)};
    append_json_escaped (in2, 1, out);
    CHECK (out == "\\u001f");
  }

  SECTION (
      "printable ASCII and non-ASCII bytes pass through "
      "unchanged"
  )
  {
    std::string out;
    const std::string in = "hello";
    append_json_escaped (in.data(), in.size(), out);
    CHECK (out == "hello");

    out.clear();
    const char in2[] = {static_cast<char> (0xC3)};
    append_json_escaped (in2, 1, out);
    REQUIRE (out.size() == 1);
    CHECK (static_cast<unsigned char> (out[0]) == 0xC3);
  }

  SECTION ("empty input appends nothing")
  {
    std::string out = "unchanged";
    append_json_escaped (nullptr, 0, out);
    CHECK (out == "unchanged");
  }
}

/* ---- aux1_to_json ----
 * Raw buffers hand-built per htslib's on-wire aux tag encoding: 2-byte
 * tag name, 1-byte type, then type-specific value bytes. p_aux1 points
 * at the type byte (2 bytes past the buffer start); p_auxEnd bounds the
 * whole buffer. */

TEST_CASE (
    "aux1_to_json converts scalar/char/string tags", "[ingest]"
)
{
  SECTION ("numeric scalar (type 'c', int8)")
  {
    uint8_t buf[] = {'N', 'M', 'c', 5};
    std::string out;
    auto r = aux1_to_json (buf + 2, buf + sizeof (buf), out);
    REQUIRE (r);
    CHECK (out == "\"NM\":5");
  }

  SECTION ("char (type 'A')")
  {
    uint8_t buf[] = {'X', 'A', 'A', 'Q'};
    std::string out;
    auto r = aux1_to_json (buf + 2, buf + sizeof (buf), out);
    REQUIRE (r);
    CHECK (out == "\"XA\":\"Q\"");
  }

  SECTION ("string (type 'Z')")
  {
    uint8_t buf[] = {'R', 'G', 'Z', 's', 'a', 'm',
                     'p', 'l', 'e', '1', '\0'};
    std::string out;
    auto r = aux1_to_json (buf + 2, buf + sizeof (buf), out);
    REQUIRE (r);
    CHECK (out == "\"RG\":\"sample1\"");
  }

  SECTION ("string value containing '\"' and '\\' gets escaped")
  {
    uint8_t buf[] = {'Z', 'Z',  'Z', 'a', '"',
                     'b', '\\', 'c', '\0'};
    std::string out;
    auto r = aux1_to_json (buf + 2, buf + sizeof (buf), out);
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
    CHECK (out == expected);
  }
}

TEST_CASE (
    "aux1_to_json handles zero-, one-, and multi-element 'B' "
    "arrays",
    "[ingest]"
)
{
  // Regression coverage for the 'B' branch: htslib only emits a
  // leading ',' once there's at least one element, so the zero-element
  // case must short-circuit rather than read past the formatted
  // buffer, and neither case should leave a trailing comma before ']'.
  SECTION ("zero elements")
  {
    uint8_t buf[] = {'X', 'Y', 'B', 'c', 0, 0, 0, 0};
    std::string out;
    auto r = aux1_to_json (buf + 2, buf + sizeof (buf), out);
    REQUIRE (r);
    CHECK (out == "\"XY\":[]");
  }

  SECTION ("single element")
  {
    uint8_t buf[] = {'X', 'Y', 'B', 'c', 1, 0, 0, 0, 5};
    std::string out;
    auto r = aux1_to_json (buf + 2, buf + sizeof (buf), out);
    REQUIRE (r);
    CHECK (out == "\"XY\":[5]");
  }

  SECTION ("three elements")
  {
    uint8_t buf[] = {'X', 'Y', 'B', 'c', 3, 0, 0, 0, 1, 2, 3};
    std::string out;
    auto r = aux1_to_json (buf + 2, buf + sizeof (buf), out);
    REQUIRE (r);
    CHECK (out == "\"XY\":[1,2,3]");
  }
}

/* ---- bind_pileup_fields / NULL conventions / schema CHECK interaction ---- */

TEST_CASE (
    "bind_pileup_fields NULL-handling conventions", "[ingest]"
)
{
  PileupDB db;
  REQUIRE (init_db (db));
  auto lociId = insert_loci (
      db, LocusData{
              .contig = "chr1",
              .pos = 100,
              .start = 100,
              .end = 150,
              .refSlice = std::nullopt
          }
  );
  REQUIRE (lociId);

  auto stmtRet = prepare_insert_reads_stmt (db);
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};

  auto pf = make_basic_fields();
  // mtidName empty, mStart < 0, auxJson empty -- all should round-trip
  // to SQL NULL, not empty-string/zero/"{}".
  REQUIRE (bind_pileup_fields (stmt, *lociId, pf) == SQLITE_OK);
  REQUIRE (sqlite3_step (stmt) == SQLITE_DONE);

  sqlite3_stmt* o_stmt = NULL;
  const std::string_view sql = "SELECT * FROM reads;";
  REQUIRE (
      sqlite3_prepare_v2 (
          db, sql.data(), static_cast<int> (sql.size()), &o_stmt,
          NULL
      ) == SQLITE_OK
  );
  auto stepRet = next_read (o_stmt, db);
  REQUIRE (stepRet);
  REQUIRE (*stepRet);

  CHECK (get_mtid (o_stmt) == "");
  CHECK (get_mstart (o_stmt) == -1);
  CHECK (get_tags (o_stmt) == "");  // NULL, not "{}"

  sqlite3_finalize (o_stmt);
}

TEST_CASE (
    "CHECK(json_valid(tags)) accepts the aux1_to_json "
    "output",
    "[ingest]"
)
{
  PileupDB db;
  REQUIRE (init_db (db));
  auto lociId = insert_loci (
      db, LocusData{
              .contig = "chr1",
              .pos = 100,
              .start = 100,
              .end = 150,
              .refSlice = std::nullopt
          }
  );
  REQUIRE (lociId);

  SECTION ("valid JSON is accepted")
  {
    auto stmtRet = prepare_insert_reads_stmt (db);
    REQUIRE (stmtRet);
    auto stmt{std::move (*stmtRet)};

    auto pf = make_basic_fields();
    pf.auxJson = "{\"XY\":[1,2,3]}";
    REQUIRE (
        bind_pileup_fields (stmt, *lociId, pf) == SQLITE_OK
    );
    CHECK (sqlite3_step (stmt) == SQLITE_DONE);
  }

  SECTION (
      "deliberately invalid JSON is rejected by the CHECK "
      "constraint"
  )
  {
    auto stmtRet = prepare_insert_reads_stmt (db);
    REQUIRE (stmtRet);
    auto stmt{std::move (*stmtRet)};

    auto pf = make_basic_fields();
    pf.auxJson =
        "{\"XY\":[1,2,3,]}";  // the pre-fix (buggy) shape
    REQUIRE (
        bind_pileup_fields (stmt, *lociId, pf) == SQLITE_OK
    );
    CHECK (sqlite3_step (stmt) == SQLITE_CONSTRAINT);
  }
}

/* ---- transaction wrappers ---- */

TEST_CASE (
    "begin_transaction/commit wrap a successful insert",
    "[ingest]"
)
{
  PileupDB db;
  REQUIRE (init_db (db));
  auto lociId = insert_loci (
      db, LocusData{
              .contig = "chr1",
              .pos = 100,
              .start = 100,
              .end = 150,
              .refSlice = std::nullopt
          }
  );
  REQUIRE (lociId);

  REQUIRE (begin_transaction (db));
  auto stmtRet = prepare_insert_reads_stmt (db);
  REQUIRE (stmtRet);
  auto stmt{std::move (*stmtRet)};
  auto pf = make_basic_fields();
  REQUIRE (bind_pileup_fields (stmt, *lociId, pf) == SQLITE_OK);
  REQUIRE (sqlite3_step (stmt) == SQLITE_DONE);
  REQUIRE (commit (db));

  sqlite3_stmt* o_stmt = NULL;
  const std::string_view sql = "SELECT COUNT(*) FROM reads;";
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
