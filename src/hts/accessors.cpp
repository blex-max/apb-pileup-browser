#include "hts/accessors.hpp"
#include "hts/types.hpp"

#include <htslib/hts.h>
#include <string>

#include <htslib/sam.h>

namespace htsacc {

AlnFile::~AlnFile() noexcept {
    if (idx) hts_idx_destroy(idx);
    if (hdr) sam_hdr_destroy(hdr);
    if (f)   hts_close(f);
}

AlnFile::AlnFile(AlnFile&& o) noexcept
    : f{o.f}, hdr{o.hdr}, idx{o.idx}
{
    o.f = nullptr; o.hdr = nullptr; o.idx = nullptr;
}

AlnFile& AlnFile::operator=(AlnFile&& o) noexcept {
    if (this != &o) {
        if (idx) hts_idx_destroy(idx);
        if (hdr) sam_hdr_destroy(hdr);
        if (f)   hts_close(f);
        f = o.f; hdr = o.hdr; idx = o.idx;
        o.f = nullptr; o.hdr = nullptr; o.idx = nullptr;
    }
    return *this;
}


hts_pos_t qpos (const bam_pileup1_t* p1) {
  return p1->qpos;
}


hts_pos_t gstart (const bam_pileup1_t* p1) {
  return p1->b->core.pos;
}

char pileup_base (const bam_pileup1_t* p1) {
  return seq_nt16_str[bam_seqi(bam_get_seq(p1->b), p1->qpos)];
}

uint16_t flag (const bam_pileup1_t* p1) {
  return p1->b->core.flag;
}


std::string seq (const bam_pileup1_t* p1, size_t qpos, size_t n) {
  std::string seq_out{};

  if (n == 0 || (qpos + n) > p1->b->core.l_qseq) {
    n = p1->b->core.l_qseq;
  }

  const auto seq_nib = bam_get_seq(p1->b);

  for (size_t i = qpos; i < n; ++i) {
    seq_out += seq_nt16_str[bam_seqi(seq_nib, i)];
  }

  return seq_out;
}


std::string get_seq_genomic (const bam_pileup1_t* p1, size_t gpos, size_t n=0) {
  const auto qpos = gpos - p1->b->core.pos;
  return seq (p1, qpos, n);
}

uint8_t base_qual (const bam_pileup1_t *p1) {
  const auto qpos = p1->qpos;

  const auto bq = bam_get_qual(p1->b);  // get qual string arr
  if (bq == NULL) {
    return 0;
  }
  return *(bq + qpos);
}

}  // end namespace


