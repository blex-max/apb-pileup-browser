#include "hts/accessors.hpp"

#include <htslib/hts.h>
#include <htslib/sam.h>

namespace htsacc {

auto start (const bam_pileup1_t* p1) {
  return p1->b->core.pos;
}

auto base (const bam_pileup1_t* p1) {
  return seq_nt16_str[bam_seqi(bam_get_seq(p1->b), p1->qpos)];
}

auto mapq (const bam_pileup1_t* p1) {
  return p1->b->core.qual;
}

auto mtid (const bam_pileup1_t* p1) {
  return p1->b->core.mtid;
}

auto mstart (const bam_pileup1_t* p1) {
  return p1->b->core.mpos;
}

auto flag (const bam_pileup1_t* p1) {
  return p1->b->core.flag;
}

auto qlen (const bam_pileup1_t* p1) {
  return p1->b->core.l_qseq;
}

auto base_qual (const bam_pileup1_t *p1) {
  const auto qpos = p1->qpos;

  const auto bq = bam_get_qual(p1->b);  // get qual arr
  if (bq == NULL) {
    return (uint8_t)0;
  }
  return *(bq + qpos);
}

std::string seq (const bam_pileup1_t* p1, size_t qpos, size_t n) {
  std::string seq_out{};
  size_t seq_len = static_cast<size_t> (qlen(p1));

  if (n == 0 || (qpos + n) > seq_len) {
    n = seq_len;
  }

  const auto seq_nib = bam_get_seq(p1->b);

  for (size_t i = qpos; i < n; ++i) {
    seq_out += seq_nt16_str[bam_seqi(seq_nib, i)];
  }

  return seq_out;
}


std::string qual_ascii (const bam_pileup1_t* p1, size_t qpos, size_t n) {
  std::string qual_out{};  // ascii
  size_t seq_len = static_cast<size_t> (qlen(p1));

  if (n == 0 || (qpos + n) > seq_len) {
    n = seq_len;
  }

  const auto qual = bam_get_qual(p1->b);
  if (qual[0] == 255) {
    qual_out = '*';
  }
  else {
    for (size_t i = qpos; i < n; ++i) {
      qual_out += (char)(qual[i] + 33);
    }
  }

  return qual_out;
}

}  // end namespace
