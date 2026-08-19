#pragma once

#include <htslib/faidx.h>
#include <htslib/hts.h>
#include <htslib/sam.h>

#include <expected>
#include <functional>
#include <string>

#include "shared/err.hpp"

struct GenomicSpan {
  hts_pos_t start;
  hts_pos_t end;
};
struct PileupPosition {
  int32_t tid;
  hts_pos_t pos;
};

// resolve tid to name
using Tid2StrFn = std::function<const char*(int)>;

struct AlnFile {
  htsFile* o_fh = NULL;
  sam_hdr_t* o_hdr = NULL;
  hts_idx_t* o_idx = NULL;

  AlnFile() = default;
  AlnFile (const AlnFile&) = delete;
  AlnFile& operator= (const AlnFile&) = delete;

  ~AlnFile() noexcept
  {
    if (o_idx != nullptr) {
      hts_idx_destroy (o_idx);
    }
    if (o_hdr != nullptr) {
      sam_hdr_destroy (o_hdr);
    }
    if (o_fh != nullptr) {
      hts_close (o_fh);
    }
  }

  AlnFile (AlnFile&& o) noexcept
      : o_fh{o.o_fh}, o_hdr{o.o_hdr}, o_idx{o.o_idx}
  {
    o.o_fh = NULL;
    o.o_hdr = NULL;
    o.o_idx = NULL;
  }

  AlnFile& operator= (AlnFile&& o) noexcept
  {
    if (this != &o) {
      if (o_idx != nullptr) {
        hts_idx_destroy (o_idx);
      }
      if (o_hdr != nullptr) {
        sam_hdr_destroy (o_hdr);
      }
      if (o_fh != nullptr) {
        hts_close (o_fh);
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

using AlnOrErr = std::expected<AlnFile, Err>;
[[nodiscard]] AlnOrErr load_aln (const char* br_fn);

struct FastaFile {
  faidx_t* o_fai;

  operator faidx_t*() const noexcept { return o_fai; }

  FastaFile() = default;
  FastaFile (const FastaFile& o) = delete;
  FastaFile& operator= (const FastaFile& o) = delete;

  FastaFile (FastaFile&& o) noexcept : o_fai{o.o_fai}
  {
    o.o_fai = nullptr;
  }

  FastaFile& operator= (FastaFile&& o) noexcept
  {
    if (this != &o) {
      if (o_fai != nullptr) {
        fai_destroy (o_fai);
      }
      o_fai = o.o_fai;
      o.o_fai = nullptr;
    }
    return *this;
  }

  ~FastaFile()
  {
    if (o_fai != nullptr) {
      fai_destroy (o_fai);
    }
  }
};

using FastaOrErr = std::expected<FastaFile, Err>;
[[nodiscard]] FastaOrErr load_fasta (const char* br_fn);

using RefSliceOrErr = std::expected<std::string, Err>;
[[nodiscard]] RefSliceOrErr fetch_region (
    const FastaFile& ff, const std::string_view contigName,
    hts_pos_t regStart, hts_pos_t regEnd
);
