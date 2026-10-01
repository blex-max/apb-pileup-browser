#pragma once

#include <cstdint>

// pileup max depth
constexpr int32_t kMaxReads = 100000;

// max memory usage for sqlite
// N * 1mb
// To prevent accidentally loading a node-destroying pileup
constexpr int64_t kMaxSqliteHeapBytes =
    static_cast<int64_t> (1000) * 1000000;
