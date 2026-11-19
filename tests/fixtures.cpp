#include "fixtures.hpp"

#include <fmt/format.h>
#include <htslib/sam.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "apb_db/apb_db_core.hpp"
#include "doctest.h"

namespace test {

sqlite3* new_db()
{
  sqlite3* conn = nullptr;
  REQUIRE (sqlite3_open (":memory:", &conn) == SQLITE_OK);
  REQUIRE (apb_create_schema (conn) == SQLITE_OK);
  return conn;
}

namespace {

// A single aligned run, all 'A' at qual 30.
// No mate, no tags.
apb_input_record1_t make_record (const TestRecord& tr, int64_t pos_0ho)
{
  apb_input_record1_t rec;
  rec.qName = tr.qname;
  rec.flag = tr.flag;
  rec.start_0ho = tr.start_0ho;
  rec.end_0ho = tr.end_0ho;
  rec.mapQ = tr.mapQ;
  rec.qPos_0ho = static_cast<int32_t> (pos_0ho - tr.start_0ho);
  rec.indel = 0;
  rec.isHead = rec.qPos_0ho == 0;
  // use simple fixed values for testing
  constexpr char k_base = 'A';
  constexpr uint8_t k_qual = 30;
  const auto readLen = static_cast<size_t> (tr.end_0ho - tr.start_0ho);
  const auto cigel =
      static_cast<uint32_t> (bam_cigar_gen (readLen, BAM_CMATCH));
  rec.isTail = static_cast<size_t> (rec.qPos_0ho) == readLen - 1;
  rec.base = k_base;
  rec.baseQual = k_qual;
  rec.seq = std::string (readLen, k_base);
  rec.rawQual = std::vector<uint8_t> (readLen, k_qual);
  rec.cigOps = {cigel};
  rec.cigStr = std::to_string (readLen) + bam_cigar_opchr (cigel);
  return rec;
}

}  // namespace

void seed_conn_with_reads (
    sqlite3* conn, std::span<const TestAln> alns,
    const std::string& contig, int64_t pos_0ho, bool withRef
)
{
  int64_t spanStart_0ho = INT64_MAX;
  int64_t spanEnd_0ho = 0;
  std::vector<apb_input_record1_t> fReads;
  for (size_t i = 0; i < alns.size(); i++) {
    const auto& f = alns[i];
    const auto alnId = apb_insert_aln (conn, f.path, f.hasContig);
    REQUIRE (alnId == i + 1);
    if (f.reads) {
      fReads.clear();
      for (const auto& r : *f.reads) {
        fReads.push_back (make_record (r, pos_0ho));
        spanStart_0ho = std::min (spanStart_0ho, r.start_0ho);
        spanEnd_0ho = std::max (spanEnd_0ho, r.end_0ho);
      }
      REQUIRE (apb_insert_records (conn, fReads, *alnId) == SQLITE_OK);
    }
  }

  // locus last: it counts the alignment files
  std::string ref;
  if (withRef) {
    for (hts_pos_t i = 0; i < spanEnd_0ho - spanStart_0ho; ++i) {
      ref += "ACGT"[i % 4];
    }
  }
  REQUIRE (
      apb_insert_locus (
          conn, contig, pos_0ho, spanStart_0ho, spanEnd_0ho,
          withRef ? std::optional<std::string_view>{ref} : std::nullopt
      ) == SQLITE_OK
  );
}

}  // namespace test
