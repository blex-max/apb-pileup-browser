#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>

// Shared test fixtures, defined in backend/hts_sql.cpp.
namespace test {

// db seeded with one file (id 1), locus metadata (chr1:150, span
// 100-200) and reads readA/readB/readC.
void seed_reads (
    sqlite3* conn, int16_t nAln = 1,
    std::optional<std::string> refSlice = std::nullopt
);

}  // namespace test
