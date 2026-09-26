#pragma once

#include <htslib/faidx.h>
#include <htslib/hts.h>
#include <htslib/sam.h>

#include <expected>
#include <functional>
#include <string>

struct GenomicSpan {
  // 0-indexed htslib-side span
  hts_pos_t start;
  hts_pos_t end;

  bool valid() const noexcept { return start >= 0 && end > start; }
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

  enum LoadErrCodes : uint8_t {
    openFail,
    hdrReadFail,
    indexLoadFail,
  };
  static std::expected<AlnFile, LoadErrCodes> load_aln (
      const std::string& path
  );
};

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

  enum LoadErrCodes : uint8_t {
    openFail,
  };
  static std::expected<FastaFile, LoadErrCodes> load_fasta (
      const char* path
  );
};

// TODO review pileup machinery
struct PileupCapture {
  htsFile* br_fh = nullptr;  // borrowed
  hts_itr_t* o_it = nullptr;
};
struct PileupIterator {
  PileupCapture* o_cap = nullptr;
  bam_plp_t o_plp = nullptr;
  const bam_pileup1_t* br_plpArr = nullptr;
  size_t nPlp = 0;
  int32_t tid = -1;
  hts_pos_t pos = -1;
  GenomicSpan span{-1, -1};

  ~PileupIterator()
  {
    if (o_cap != nullptr) {
      hts_itr_destroy (o_cap->o_it);
      delete o_cap;
    }
    if (o_plp != nullptr) {
      bam_plp_destroy (o_plp);
    }
    br_plpArr = nullptr;
  }
  PileupIterator() = default;
  PileupIterator (PileupIterator&) = delete;
  PileupIterator& operator= (PileupIterator&) = delete;
  PileupIterator (PileupIterator&& o) noexcept
      : o_cap (o.o_cap),
        o_plp (o.o_plp),
        br_plpArr (o.br_plpArr),
        nPlp (o.nPlp),
        tid (o.tid),
        pos (o.pos),
        span (o.span)
  {
    o.o_cap = nullptr;
    o.o_plp = nullptr;
    o.br_plpArr = nullptr;
    o.nPlp = 0;
  };
  PileupIterator& operator= (PileupIterator&&) = delete;

  enum ConstructErrCodes : uint8_t {
    samItrFail,
    pileupInitFail,
    pileupIterateFail,
    locusNotCovered
  };
  static std::expected<PileupIterator, ConstructErrCodes>
  prepare_pileup_iter (
      const AlnFile& aln, int32_t tid, hts_pos_t pos
  );
};
