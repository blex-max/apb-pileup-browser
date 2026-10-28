#include "fixtures.hpp"

#include <fmt/format.h>
#include <htslib/sam.h>

#include <filesystem>

#include "doctest.h"
#include "model/hts2apb.hpp"
#include "model/schema.hpp"
#include "shared/cleanup.hpp"

namespace test {

void exec_ok (sqlite3* conn, const char* sql)
{
  REQUIRE (
      sqlite3_exec (conn, sql, nullptr, nullptr, nullptr) == SQLITE_OK
  );
}

void seed_file (sqlite3* conn)
{
  exec_ok (
      conn,
      "INSERT INTO alignment_files (id, path, has_contig, n_reads) "
      "VALUES (1, 'test.bam', 1, 1);"
  );
}

// seed a fresh db with locus metadata and three reads
void seed_reads (
    sqlite3* conn, int16_t nAln, std::optional<std::string_view> refSlice
)
{
  seed_file (conn);
  REQUIRE (
      apb_insert_locus (
          conn, "chr1", 149, GenomicSpan{.start = 99, .end = 200}, refSlice
      ) == SQLITE_OK
  );
  // apb_insert_locus counts alignment_files rows; tests may want more
  if (nAln != 1) {
    REQUIRE (
        sqlite3_exec (
            conn,
            fmt::format ("UPDATE locus_metadata SET n_aln = {};", nAln)
                .c_str(),
            nullptr, nullptr, nullptr
        ) == SQLITE_OK
    );
  }

  sqlite3_stmt* o_stmt = nullptr;
  REQUIRE (
      sqlite3_prepare_v2 (
          conn, schema::sqlInsertReads.data(),
          static_cast<int> (schema::sqlInsertReads.size()), &o_stmt, NULL
      ) == SQLITE_OK
  );
  Defer stmtCleanup ([&]() { sqlite3_finalize (o_stmt); });

  // (qname, flag, mapq, start, end), 1-based as stored -- all satisfy
  // 100 <= start <= 150 <= end <= 200.
  struct Row {
    const char* qname;
    int flag, mapq, start, end;
  };
  const Row rows[] = {
      {"readA", 99, 60, 100, 170},
      {"readB", 4, 10, 120, 190},
      {"readC", 99, 45, 140, 200},
  };
  const uint8_t qual[] = {30};
  const uint32_t cig[] = {
      static_cast<uint32_t> (bam_cigar_gen (1, BAM_CMATCH))
  };
  for (const auto& r : rows) {
    int c = 1;
    sqlite3_bind_text (o_stmt, c++, r.qname, -1, SQLITE_STATIC);
    sqlite3_bind_int (o_stmt, c++, r.flag);
    sqlite3_bind_int (o_stmt, c++, r.start);
    sqlite3_bind_int (o_stmt, c++, r.end);
    sqlite3_bind_int (o_stmt, c++, r.mapq);
    sqlite3_bind_text (o_stmt, c++, "A", -1, SQLITE_STATIC);  // base
    sqlite3_bind_int (o_stmt, c++, 30);  // basequal
    sqlite3_bind_int (o_stmt, c++, 1);  // qpos
    sqlite3_bind_int (o_stmt, c++, 0);  // indel
    sqlite3_bind_int (o_stmt, c++, 0);  // is_del
    sqlite3_bind_int (o_stmt, c++, 0);  // is_head
    sqlite3_bind_int (o_stmt, c++, 0);  // is_tail
    sqlite3_bind_int (o_stmt, c++, 0);  // is_refskip
    sqlite3_bind_text (o_stmt, c++, "1M", -1, SQLITE_STATIC);
    sqlite3_bind_text (o_stmt, c++, "A", -1, SQLITE_STATIC);  // seq
    sqlite3_bind_blob (o_stmt, c++, qual, 1, SQLITE_STATIC);
    sqlite3_bind_null (o_stmt, c++);  // mtid
    sqlite3_bind_null (o_stmt, c++);  // mstart
    sqlite3_bind_null (o_stmt, c++);  // tags
    sqlite3_bind_blob (o_stmt, c++, cig, sizeof (cig), SQLITE_STATIC);
    sqlite3_bind_int (o_stmt, c++, 1);  // ncig
    sqlite3_bind_int64 (o_stmt, c++, k_testFileId);
    REQUIRE (sqlite3_step (o_stmt) == SQLITE_DONE);
    sqlite3_reset (o_stmt);
    sqlite3_clear_bindings (o_stmt);
  }
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
