#include "fixtures.hpp"

#include <fmt/format.h>
#include <htslib/sam.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "doctest.h"
#include "model/hts2apb.hpp"
#include "model/schema.hpp"
#include "shared/cleanup.hpp"

namespace test {

sqlite3* new_db()
{
  sqlite3* conn = nullptr;
  REQUIRE (sqlite3_open (":memory:", &conn) == SQLITE_OK);
  REQUIRE (apb_create_schema (conn) == SQLITE_OK);
  return conn;
}

void exec_ok (sqlite3* conn, const char* sql)
{
  REQUIRE (
      sqlite3_exec (conn, sql, nullptr, nullptr, nullptr) == SQLITE_OK
  );
}

namespace {

// A read overlapping the seeded pileup position.
struct SeedRead {
  const char* qname;
  uint16_t flag;
  uint8_t mapQ;
  hts_pos_t start, end;  // 1-based closed, as stored in the db
};

// 0-based, as htslib
constexpr hts_pos_t k_seedPos = 149;

// Built as demo.cpp builds its synthetic reads: a bam1_t, with the
// pileup fields set by hand. A single aligned run, all 'A' at qual 30.
// Owner must bam_destroy1 .b
bam_pileup1_t make_pileup1 (const SeedRead& r)
{
  const hts_pos_t start0 = r.start - 1;
  const auto readLen = static_cast<size_t> (r.end - start0);
  const uint32_t cigar[] = {
      static_cast<uint32_t> (bam_cigar_gen (readLen, BAM_CMATCH))
  };
  const std::string seq (readLen, 'A');
  const std::vector<char> qual (readLen, 30);

  bam_pileup1_t p1{};
  p1.b = bam_init1();
  REQUIRE (p1.b != nullptr);
  REQUIRE (
      bam_set1 (
          p1.b, std::strlen (r.qname), r.qname, r.flag, -1 /* no tid */,
          start0, r.mapQ, 1, cigar, -1 /* no mate */, -1, -1, readLen,
          seq.data(), qual.data(), 0 /* no tags */
      ) >= 0
  );
  p1.qpos = static_cast<int32_t> (k_seedPos - start0);
  p1.indel = 0;
  p1.is_head = (p1.qpos == 0);
  p1.is_tail = (static_cast<size_t> (p1.qpos) == readLen - 1);
  return p1;
}

}  // namespace

void seed_reads (
    sqlite3* conn, const std::vector<ExtraFile>& extraFiles, bool withRef
)
{
  // (qname, flag, mapq, start, end); all satisfy
  // 100 <= start <= 150 <= end <= 200.
  const SeedRead seedReads[] = {
      {"readA", 99, 60, 100, 170},
      {"readB", 4, 10, 120, 190},
      {"readC", 99, 45, 140, 200},
  };
  std::vector<bam_pileup1_t> plp;
  Defer plpCleanup ([&]() {
    for (auto& p1 : plp) {
      bam_destroy1 (p1.b);
    }
  });
  for (const auto& r : seedReads) {
    plp.push_back (make_pileup1 (r));
  }

  const auto alnId = apb_insert_aln (
      conn, "test.bam", true, static_cast<int64_t> (plp.size())
  );
  REQUIRE (alnId);
  REQUIRE (*alnId == k_testFileId);
  for (const auto& f : extraFiles) {
    REQUIRE (apb_insert_aln (conn, f.path, f.hasContig, f.nReads));
  }

  REQUIRE (apb_insert_pileup (conn, plp, *alnId, [] (int) -> const char* {
    return nullptr;
  }));

  // locus last: it counts the alignment files
  const GenomicSpan span{.start = 99, .end = 200};
  std::string ref;
  if (withRef) {
    for (hts_pos_t i = 0; i < span.end - span.start; ++i) {
      ref += "ACGT"[i % 4];
    }
  }
  REQUIRE (
      apb_insert_locus (
          conn, "chr1", k_seedPos, span,
          withRef ? std::optional<std::string_view>{ref} : std::nullopt
      ) == SQLITE_OK
  );
}

std::filesystem::path temp_db_path (std::string_view name)
{
  return std::filesystem::temp_directory_path() / name;
}

// Every column of every row of `sql`'s result, as one string.
std::string digest (sqlite3* conn, const char* sql)
{
  sqlite3_stmt* o_stmt = nullptr;
  REQUIRE (sqlite3_prepare_v2 (conn, sql, -1, &o_stmt, NULL) == SQLITE_OK);
  Defer stmtCleanup ([&]() { sqlite3_finalize (o_stmt); });
  std::string out;
  while (sqlite3_step (o_stmt) == SQLITE_ROW) {
    for (int i = 0; i < sqlite3_column_count (o_stmt); ++i) {
      if (sqlite3_column_type (o_stmt, i) == SQLITE_NULL) {
        out += "NULL";
      }
      else if (sqlite3_column_type (o_stmt, i) == SQLITE_BLOB) {
        const auto* br_blob =
            static_cast<const uint8_t*> (sqlite3_column_blob (o_stmt, i));
        for (int j = 0; j < sqlite3_column_bytes (o_stmt, i); ++j) {
          out += fmt::format ("{:02x}", br_blob[j]);
        }
      }
      else {
        out += reinterpret_cast<const char*> (
            sqlite3_column_text (o_stmt, i)
        );
      }
      out += '|';
    }
    out += '\n';
  }
  return out;
}

}  // namespace test
