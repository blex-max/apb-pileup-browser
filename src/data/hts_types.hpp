#pragma once

#include <fmt/format.h>
#include <htslib/hts.h>

#include <string>

struct GenomicSpan {
  // 0-indexed htslib-side span
  hts_pos_t start;
  hts_pos_t end;

  bool valid() const noexcept { return start >= 0 && end > start; }

  std::string to_string() const
  {
    return fmt::format ("GenomicSpan{{start: {}, end: {}}}", start, end);
  }
};
