#pragma once

#include <htslib/sam.h>

#include <string>
#include <string_view>

// TODO: move to src file
// NOTE: output parameter would save some allocations
inline std::string expand_sequence (
    std::string_view seq, const uint32_t* cig,
    size_t nCig/*, std::optional<std::string> ref_slice */
)
{
  std::string out;

  size_t iSeq = 0;
  for (size_t iOp = 0; iOp < nCig; iOp++) {
    const auto op = cig[iOp];
    const auto opSz = bam_cigar_oplen (op);
    const auto opType = bam_cigar_type (op);
    if (opType == 0b11) {
      // consumes query and ref
      // insert oplen bases into out as lifted directly from input seq
      out.append (seq.data() + iSeq, opSz);
      iSeq += opSz;
    }
    else if (opType == 0b01) {
      // consumes ref only
      // insert oplen dashes (-) into out
      // don't advance iSeq
      out.append (std::string (opSz, '-'));
    }
    else if (opType == 0b10) {
      // consumes query only
      iSeq += opSz;
    }
  }

  return out;
}

inline std::pair<std::string, std::string> get_soft_clips (
    const uint32_t* cig, size_t nCig
)
{
  std::pair<std::string, std::string> out;
  const auto firstOp = cig[0];
  const auto lastOp = cig[nCig];
  if (bam_cigar_op (firstOp) == BAM_CSOFT_CLIP) {
    const auto opSz = bam_cigar_oplen (firstOp);
    out.first = "s(" + std::to_string (opSz) + ")";
  }
  if (bam_cigar_op (lastOp) == BAM_CSOFT_CLIP) {
    const auto opSz = bam_cigar_oplen (lastOp);
    out.second = "s(" + std::to_string (opSz) + ")";
  }

  return out;
}
