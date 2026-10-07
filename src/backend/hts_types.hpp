#pragma once

#include <fmt/format.h>
#include <htslib/faidx.h>
#include <htslib/hts.h>
#include <htslib/sam.h>

#include <functional>
#include <string>

struct GenomicSpan {
  // 0-indexed htslib-side span
  hts_pos_t start;
  hts_pos_t end;

  bool valid() const noexcept { return start >= 0 && end > start; }
};

template <>
struct fmt::formatter<GenomicSpan> : fmt::formatter<std::string> {
  auto format (const GenomicSpan& s, format_context& ctx) const
  {
    return fmt::formatter<std::string>::format (
        fmt::format ("GenomicSpan{{start: {}, end: {}}}", s.start, s.end),
        ctx
    );
  }
};

// resolve tid to name
using Tid2StrFn = std::function<const char*(int)>;
