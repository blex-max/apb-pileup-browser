#pragma once

#include <sqlite3.h>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// ingest pileup from disk
// returns expected void/str since any failure
// is a top-level crash which can be fully described
// and exited upon.
[[nodiscard]] std::expected<void, std::string> ingest_sam_auto (
    sqlite3* conn, const std::vector<std::filesystem::path>& alnPaths,
    const std::string& locus,
    const std::optional<std::filesystem::path>& refPath, bool zeroBased,
    bool limitMemUsage
);

void ingest_demo_auto (sqlite3* conn);
