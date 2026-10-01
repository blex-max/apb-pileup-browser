#pragma once

#include <cstdint>

// pileup max depth
constexpr int32_t kMaxReads = 10'0000;

// max memory usage for sqlite
// N * 1mb
// To prevent accidentally loading a node-destroying pileup
constexpr int64_t kMaxSqliteHeapBytes =
    static_cast<int64_t> (2000) * 1'000'000;
