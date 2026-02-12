#include <cstdint>
#include <string>
#include <vector>

// to display sequence
struct QueryRep {
  uint64_t id, start;
  std::string q;
};

struct RefRep {
  std::string r;
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
inline auto make_test_display_data (size_t n_query) {
  std::tuple<RefRep, Queries> d{};

  std::get<RefRep>(d).r = random_base_seq(300);

  auto &qv = std::get<std::vector<QueryRep>>(d);
  for (size_t i = 0; i < n_query; ++i) {
    qv.emplace_back(i, 0, random_base_seq(150));
  }

  return d;
}
