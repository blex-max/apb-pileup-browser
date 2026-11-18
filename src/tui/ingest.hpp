#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using conn_or_failmsg_t = std::expected<sqlite3*, std::string>;

// where a newly built db lives
struct db_target_t {
  enum Kind : uint8_t {
    tempFile,  // removed on close
    file,  // must not already exist; removed on failure
  };
  Kind kind = tempFile;
  std::filesystem::path path;  // file only
};

// ingest pileup from disk
// returns expected conn/str since any failure
// is a top-level crash which can be fully described
// and exited upon.
[[nodiscard]] conn_or_failmsg_t ingest_sam_auto (
    const db_target_t& target, const std::string& locus,
    bool zeroBasedLocus,
    const std::vector<std::filesystem::path>& alnPaths,
    const std::optional<std::filesystem::path>& refPath,
    std::optional<size_t> loadLimitBytes,
    std::optional<size_t> loadLimitDepth
);

// opens the db at dbInputPath directly, read-only
[[nodiscard]] conn_or_failmsg_t ingest_db_auto (
    const std::filesystem::path& dbInputPath
);

[[nodiscard]] conn_or_failmsg_t ingest_demo (const db_target_t& target);
