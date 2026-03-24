#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

// to display sequence
struct QueryRep {
  uint64_t id, start;
  std::string s;
  uint8_t qual=0;  // for demo
  uint8_t flag=0;
};

struct RefRep {
  std::string s;
};


inline auto random_base_seq(size_t len) {
  static const char bases[] = "ATCG";
  std::string out;

  for (size_t i = 0; i < len; ++i) {
    out += bases[rand() % (sizeof(bases) - 1)];
  }

  return out;
}

using Queries = std::vector<QueryRep>;
using PileupDisplayBundle = std::tuple<RefRep, Queries>;
inline PileupDisplayBundle make_test_display_data (size_t width) {
  std::mt19937 rng;
  std::uniform_int_distribution<uint8_t> ud(0, 255);

  const auto qlen = (width / 2) + 1;
  std::tuple<RefRep, Queries> d{};

  const auto ref_seq = random_base_seq(width);
  std::get<RefRep>(d).s = ref_seq;

  auto &qv = std::get<std::vector<QueryRep>>(d);
  for (size_t i = 0; i < qlen; ++i) {
    qv.emplace_back(i, i, ref_seq.substr(i, qlen), ud(rng));
  }
  return d;
}
