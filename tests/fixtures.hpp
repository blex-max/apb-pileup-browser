#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Shared test fixtures, defined in tests/fixtures.cpp.
namespace test {

// alignment_files row that per-record inserts reference via path_id.
constexpr sqlite3_int64 k_testFileId = 1;

// REQUIREs sql runs OK on conn
void exec_ok (sqlite3* conn, const char* sql);

// insert alignment_files row k_testFileId
void seed_file (sqlite3* conn);

// db seeded with one file (id 1), locus metadata (chr1:150, span
// 100-200) and reads readA/readB/readC.
void seed_reads (
    sqlite3* conn, int16_t nAln = 1,
    std::optional<std::string_view> refSlice = std::nullopt
);

std::filesystem::path temp_db_path (std::string_view name);

// Every column of every row of `sql`'s result, as one string.
std::string digest (sqlite3* conn, const char* sql);

}  // namespace test
