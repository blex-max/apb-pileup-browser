#pragma once

#include <htslib/hts.h>
#include <htslib/sam.h>

struct AlnFile {
  htsFile* o_fh = NULL;
  sam_hdr_t* o_hdr = NULL;
  hts_idx_t* o_idx = NULL;

  AlnFile() = default;
  AlnFile(const AlnFile&) = delete;
  AlnFile& operator=(const AlnFile&) = delete;

  ~AlnFile() noexcept
  {
    if (o_idx) {
      hts_idx_destroy(o_idx);
    }
    if (o_hdr) {
      sam_hdr_destroy(o_hdr);
    }
    if (o_fh) {
      hts_close(o_fh);
    }
  }

  AlnFile(AlnFile&& o) noexcept
      : o_fh{o.o_fh}, o_hdr{o.o_hdr}, o_idx{o.o_idx}
  {
    o.o_fh = NULL;
    o.o_hdr = NULL;
    o.o_idx = NULL;
  }

  AlnFile& operator=(AlnFile&& o) noexcept
  {
    if (this != &o) {
      if (o_idx) {
        hts_idx_destroy(o_idx);
      }
      if (o_hdr) {
        sam_hdr_destroy(o_hdr);
      }
      if (o_fh) {
        hts_close(o_fh);
      }
      o_fh = o.o_fh;
      o_hdr = o.o_hdr;
      o_idx = o.o_idx;
      o.o_fh = NULL;
      o.o_hdr = NULL;
      o.o_idx = NULL;
    }
    return *this;
  }
};

struct GenomeSpan {
  hts_pos_t start;
  hts_pos_t end;
};
struct PileupPosition {
  int32_t tid;
  hts_pos_t pos;
};
