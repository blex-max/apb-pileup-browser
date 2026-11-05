#include "demo.hpp"

#include <fmt/format.h>
#include <htslib/sam.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "model/hts_types.hpp"
#include "shared/apb_assert.hpp"

static const char kBaseArray[] = "ACGT";

// Deterministic reference sequence
static std::string fixed_ref_seq (size_t len)
{
  std::string out;
  out.reserve (len);
  for (size_t i = 0; i < len; ++i) {
    out += kBaseArray[i % 4];
  }
  return out;
}

static char random_base (std::mt19937& rng)
{
  std::uniform_int_distribution<int> pick (0, 3);
  return kBaseArray[pick (rng)];
}

// A base guaranteed to differ from refBase, for injecting mismatches
// into otherwise ref-copied synthetic reads.
static char mutate_base (char refBase, std::mt19937& rng)
{
  char b;
  do {
    b = random_base (rng);
  } while (b == refBase);
  return b;
}

synthetic_pileup_t generate_demo_data (
    uint16_t regWidth, uint16_t nQuery, hts_pos_t pos
)
{
  APB_ASSERT ((regWidth / 2) < pos, regWidth, pos);
  // every read has a core aligned run of this length, covering the
  // pileup column.
  const auto readLen = static_cast<size_t> ((regWidth / 2) - 1);
  auto refSeq = fixed_ref_seq (regWidth);
  // refSeq[0] sits at this absolute position; pos is refSeq[readLen]
  const auto winStart = pos - static_cast<hts_pos_t> (readLen);
  // known ref base at pos
  refSeq[readLen] = 'A';

  constexpr size_t maxDelLen = 4;

  // start >= 2 leaves a base after the pileup column for an indel to follow.
  // headroom of maxDelLen keeps start+readLen+delLen within regWidth.
  std::uniform_int_distribution<size_t> gstartGen (2, readLen - maxDelLen);
  constexpr double mismatchRate = 0.01;
  std::bernoulli_distribution mismatchDist (mismatchRate);
  std::uniform_int_distribution<size_t> delLenGen (1, maxDelLen);
  constexpr size_t maxInsLen = 4;
  std::uniform_int_distribution<size_t> insLenGen (1, maxInsLen);
  constexpr size_t maxClipLen = 20;
  std::uniform_int_distribution<size_t> clipLenGen (1, maxClipLen);
  // mostly 60, some 0, a spread between
  std::discrete_distribution<int> mapQBand ({5, 15, 80});
  std::uniform_int_distribution<int> mapQMid (1, 59);
  const std::array<char, 5> qualVals{
      'F' - 33, 'E' - 33, 'D' - 33, ':' - 33, ';' - 33
  };
  std::discrete_distribution<int> qualValPicker ({80, 30, 20, 15, 15});

  // Each read carries one allele at the pileup column...
  // FIns/FDel follow the column; Del covers it.
  enum class Allele : uint8_t { Ref, Alt, FIns, FDel, Del };
  constexpr char pileupAlt = 'T';
  std::discrete_distribution<int> allelePicker (
      {0.45, 0.20, 0.12, 0.12, 0.11}
  );

  // and, independently, at most one soft clip.
  enum class AlignmentFeature : uint8_t { None, LeadClip, TailClip };
  std::discrete_distribution<int> featurePicker ({0.80, 0.10, 0.10});

  // All reads paired, mates on the same contig unless unmapped.
  std::bernoulli_distribution coinFlip (0.5);
  std::bernoulli_distribution mateUnmappedDist (0.05);
  std::uniform_int_distribution<hts_pos_t> mateDistGen (100, 400);
  // at most one of none/dup/secondary/supplementary/qcfail
  constexpr std::array<uint16_t, 5> filterFlags{
      0, BAM_FDUP, BAM_FSECONDARY, BAM_FSUPPLEMENTARY, BAM_FQCFAIL
  };
  std::discrete_distribution<int> filterPicker ({87, 8, 2, 2, 1});

  // read/ref coordinates are absolute (0-based); index refSeq via winStart
  genomic_span_t span{INT64_MAX, 0};
  std::mt19937 rng;
  synthetic_pileup_t out;
  out.p.reserve (nQuery);
  for (size_t i = 0; i < nQuery; ++i) {
    const auto readStart =
        winStart + static_cast<hts_pos_t> (gstartGen (rng));
    // offset of the pileup column within the core aligned run
    const auto colOff = static_cast<size_t> (pos - readStart);

    std::vector<uint32_t> cigOps;
    const auto pushOp = [&cigOps] (size_t len, int op) {
      cigOps.push_back (static_cast<uint32_t> (bam_cigar_gen (len, op)));
    };
    const auto allele = static_cast<Allele> (allelePicker (rng));
    const auto feature =
        static_cast<AlignmentFeature> (featurePicker (rng));

    if (feature == AlignmentFeature::LeadClip) {
      pushOp (clipLenGen (rng), BAM_CSOFT_CLIP);
    }
    int indel = 0;  // meaning per htslib p1.indel
    if (allele == Allele::Del) {
      // column falls inside the deletion; >= 1 base precedes it
      const size_t n = delLenGen (rng);
      const size_t pre =
          colOff - std::uniform_int_distribution<size_t> (0, n - 1) (rng);
      pushOp (pre, BAM_CMATCH);
      pushOp (n, BAM_CDEL);
      pushOp (readLen - pre, BAM_CMATCH);
    }
    else if (allele == Allele::FIns || allele == Allele::FDel) {
      // indel immediately after the pileup column
      const bool isDel = allele == Allele::FDel;
      const size_t n = isDel ? delLenGen (rng) : insLenGen (rng);
      pushOp (colOff + 1, BAM_CMATCH);
      pushOp (n, isDel ? BAM_CDEL : BAM_CINS);
      pushOp (readLen - colOff - 1, BAM_CMATCH);
      indel = isDel ? -static_cast<int> (n) : static_cast<int> (n);
    }
    else {
      pushOp (readLen, BAM_CMATCH);
    }
    if (feature == AlignmentFeature::TailClip) {
      pushOp (clipLenGen (rng), BAM_CSOFT_CLIP);
    }

    // Walk the CIGAR to build the read sequence, tracking ref and query
    // cursors. The pileup column is inside an M op, or a D op for Del.
    std::string seq;
    int32_t finalQPos = -1;
    bool isDel = false;
    int64_t nm = 0;  // edit distance, for the NM tag
    auto refPos = readStart;
    for (const auto cig : cigOps) {
      const size_t len = bam_cigar_oplen (cig);
      switch (bam_cigar_op (cig)) {
        case BAM_CMATCH:
          for (size_t k = 0; k < len; ++k, ++refPos) {
            const char refBase =
                refSeq[static_cast<size_t> (refPos - winStart)];
            char b;
            if (refPos == pos) {
              finalQPos = static_cast<int32_t> (seq.size());
              b = allele == Allele::Alt ? pileupAlt : refBase;
            }
            else {
              b = mismatchDist (rng) ? mutate_base (refBase, rng)
                                     : refBase;
            }
            if (b != refBase) {
              ++nm;
            }
            seq += b;
          }
          break;
        case BAM_CDEL:
          if (pos >= refPos &&
              pos < refPos + static_cast<hts_pos_t> (len)) {
            // htslib: qpos of a deleted column is the next query base
            finalQPos = static_cast<int32_t> (seq.size());
            isDel = true;
          }
          refPos += static_cast<hts_pos_t> (len);
          nm += static_cast<int64_t> (len);
          break;
        case BAM_CINS:
          nm += static_cast<int64_t> (len);
          [[fallthrough]];
        default:  // insertion, soft clip: bases not in the reference
          for (size_t k = 0; k < len; ++k) {
            seq += random_base (rng);
          }
          break;
      }
    }
    APB_ASSERT (finalQPos >= 0);

    std::vector<char> qual (seq.size());
    for (auto& q : qual) {
      q = qualVals[static_cast<size_t> (qualValPicker (rng))];
    }

    uint16_t flag = BAM_FPAIRED;
    flag |= coinFlip (rng) ? BAM_FREAD1 : BAM_FREAD2;
    flag |= filterFlags[static_cast<size_t> (filterPicker (rng))];
    const bool isRev = coinFlip (rng);
    if (isRev) {
      flag |= BAM_FREVERSE;
    }
    // unmapped mate sits at this read's position, per SAM recommended
    // practice (bwa, minimap2, fixmate, htsjdk)
    hts_pos_t mPos = readStart;
    if (mateUnmappedDist (rng)) {
      flag |= BAM_FMUNMAP;
    }
    else {
      flag |= BAM_FPROPER_PAIR;
      // FR orientation: mate on the opposite strand, facing this read
      const auto d = mateDistGen (rng);
      mPos = isRev ? readStart - d : readStart + d;
      if (!isRev) {
        flag |= BAM_FMREVERSE;
      }
    }

    int mapQ = 60;
    switch (mapQBand (rng)) {
      case 0:
        mapQ = 0;
        break;
      case 1:
        mapQ = mapQMid (rng);
        break;
      default:
        break;
    }

    const std::string qname = "read" + std::to_string (i);
    auto& p1 = out.p.emplace_back();
    p1.b = bam_init1();
    APB_ASSERT (p1.b);
    // clang-format off
    const int setRet = bam_set1(
      p1.b,
      qname.size(),
      qname.data(),
      flag,
      0,  /* single demo contig; mtid matches for '=' */
      readStart,
      static_cast<uint8_t> (mapQ),
      cigOps.size(),
      cigOps.data(),
      0,
      mPos,
      0,  /* isize not used */
      seq.size(),
      seq.data(),
      qual.data(),
      0 /* aux appended below */
    );
    // clang-format on
    APB_ASSERT (setRet >= 0, setRet);
    APB_ASSERT (
        const auto auxRet = bam_aux_update_int (p1.b, "NM", nm);
        auxRet == 0, auxRet
    );

    p1.qpos = finalQPos;
    p1.indel = indel;
    p1.is_head = (finalQPos == 0);
    p1.is_tail = (static_cast<size_t> (finalQPos) == seq.size() - 1);
    p1.is_refskip = false;  // not in demo
    p1.is_del = isDel;

    span.start = std::min (readStart, span.start);
    span.end = std::max (refPos, span.end);
  }
  out.ref = refSeq.substr (
      static_cast<size_t> (span.start - winStart),
      static_cast<size_t> (span.end - span.start)
  );
  out.pspan = span;
  return out;
}
