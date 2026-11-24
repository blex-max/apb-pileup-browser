#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Shared test fixtures, defined in tests/fixtures.cpp.
namespace test {

// A new empty in-memory apb db. Caller closes.
sqlite3* new_db();

// A simplified custom input record definition for testing purposes
struct TestRecord {
  const char* qname;
  uint16_t flag;
  uint8_t mapQ;
  int64_t start_0ho, end_0ho;  // 0-based half open
};
struct TestAln {
  std::string path;
  std::optional<std::vector<TestRecord>> reads;
  bool hasContig;
};
// Fills an empty apb db
void seed_conn_with_reads (
    sqlite3* conn, std::span<const TestAln> alns,
    const std::string& contig, int64_t pos_0ho, bool withRef
);

}  // namespace test
