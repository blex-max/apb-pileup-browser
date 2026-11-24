#pragma once

#include <sqlite3.h>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

using conn_or_failmsg_t = std::expected<sqlite3*, std::string>;

// dbOutPath: where a newly built db lives. nullopt is a temp file,
// removed on close; a path must not already exist, and is removed
// on failure.

// ingest pileup from disk
// returns expected conn/str since any failure
// is a top-level crash which can be fully described
// and exited upon.
[[nodiscard]] conn_or_failmsg_t ingest_sam (
    const std::optional<std::filesystem::path>& dbOutPath,
    const std::string& locus, bool zeroBasedLocus,
    const std::vector<std::filesystem::path>& alnPaths,
    const std::optional<std::filesystem::path>& refPath,
    std::optional<size_t> loadLimitBytes,
    std::optional<size_t> loadLimitDepth
);

// opens the db at dbInputPath directly, read-only
[[nodiscard]] conn_or_failmsg_t ingest_db (
    const std::filesystem::path& dbInputPath
);

[[nodiscard]] conn_or_failmsg_t ingest_demo (
    const std::optional<std::filesystem::path>& dbOutPath
);
