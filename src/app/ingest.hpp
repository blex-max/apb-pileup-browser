#pragma once

#include <sqlite3.h>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

/* bounds */
// max pileup size, to prevent accidentally loading
// some RAM-destroying pileup.
#define DEPTH_LIMIT 500'000
#define GB_LIMIT 4
constexpr auto kBytesLimit =
    static_cast<uint64_t> (GB_LIMIT) * 1'000'000'000;

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
