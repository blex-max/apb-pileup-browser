#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Shared test fixtures, defined in tests/fixtures.cpp.
namespace test {

// alignment_files id of the file which seed_reads' reads belong to.
constexpr sqlite3_int64 k_testFileId = 1;

// A new empty in-memory apb db. Caller closes.
sqlite3* new_db();

// REQUIREs sql runs OK on conn
void exec_ok (sqlite3* conn, const char* sql);

// an alignment file to register alongside the seeded one
struct ExtraFile {
  std::string path;
  bool hasContig;
  int64_t nReads;
};

// Fills an empty apb db as ingest does: file k_testFileId ("test.bam"),
// then any extraFiles (ids 2, 3, ...), reads readA/readB/readC for the
// first file, and locus metadata (chr1:150, span 100-200). With withRef
// the locus carries a reference slice matching its span.
void seed_reads (
    sqlite3* conn, const std::vector<ExtraFile>& extraFiles = {},
    bool withRef = false
);

std::filesystem::path temp_db_path (std::string_view name);

// Every column of every row of `sql`'s result, as one string.
std::string digest (sqlite3* conn, const char* sql);

}  // namespace test
