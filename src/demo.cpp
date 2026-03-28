#include "demo.hpp"
#include "hts/boundary-types.hpp"

#include <algorithm>
#include <cassert>
#include <htslib/sam.h>
#include <random>

namespace demo {

auto random_base_seq(size_t len) {
  static const char bases[] = "ATCG";
  std::string out;

  for (size_t i = 0; i < len; ++i) {
    out += bases[rand() % (sizeof(bases) - 1)];
  }

  return out;
}

PileupData make_demo_pileup (size_t region_width, size_t n_query) {
  std::mt19937 rng;
  std::uniform_int_distribution<uint8_t> u8_gen;

  const auto pileup_pos = (region_width / 2) - 1;
  const auto qlen = pileup_pos;
  const auto ref_seq = random_base_seq (region_width);

  std::uniform_int_distribution<size_t> gstart_gen(0, qlen);
  auto roll_gstart = [&gstart_gen, &rng] () { return gstart_gen(rng); };
  std::vector<size_t> v_query_gstart (n_query);
  std::ranges::generate (v_query_gstart, roll_gstart);
  std::sort (begin(v_query_gstart), end(v_query_gstart));  // ascencding starts for each read

  /*
    NOTE:
    rather than materialising a vector of structs, can reduce
    cost by directly iterating htslib arrays and tracking pointer
    offset.

    TODO:
    Create funcs for accessing underlying htslib data
    to avoid doing onerous htslib interaction in the UI code
  */

  auto barr =
    static_cast<bam1_t*> (calloc (n_query, sizeof (bam1_t)));
  auto p1arr =
    std::make_unique<bam_pileup1_t[]>(n_query);

  for (size_t i = 0; i < n_query; ++i) {
    auto bi = barr + i;
    auto pi = p1arr.get() + i;

    const auto q_gstart = v_query_gstart[i];

    // setup bam1
    bam_set1 (
      bi,
      0,
      NULL,
      BAM_FUNMAP,
      -1,
      q_gstart,
      0,
      0,
      NULL,
      -1,
      0,
      0,
      qlen,
      ref_seq.substr(q_gstart, qlen).c_str(),
      std::string(qlen, 'F').c_str(),
      0
    );

    // setup pileup1

    pi->b = bi;  // link
    pi->qpos = pileup_pos - q_gstart;
  }

  // I read that you can sort an array of this kind like
  // std::sort (p1arr.get(), p1arr.get() + n_query, comp)
  // but I don't know if that would invalidate anything.

  return {
    {0, region_width, pileup_pos},
    {ref_seq},
    {std::move(p1arr), 0, n_query}
  };
}

}  // end namespace
