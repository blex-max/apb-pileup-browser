#include <htslib/sam.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "app/stringify_alignment.hpp"

namespace {

uint32_t cigel (uint32_t len, uint32_t op)
{
  return static_cast<uint32_t> (bam_cigar_gen (len, op));
}

}  // namespace

TEST_CASE (
    "get_soft_clips reports clips at either end", "[stringify]"
)
{
  SECTION ("both ends clipped")
  {
    std::vector<uint32_t> cig{
        cigel (5, BAM_CSOFT_CLIP), cigel (10, BAM_CMATCH),
        cigel (3, BAM_CSOFT_CLIP)
    };
    auto [first, second] =
        get_soft_clips (cig.data(), cig.size());
    CHECK (first == "s(5)");
    CHECK (second == "s(3)");
  }

  SECTION ("no clips")
  {
    std::vector<uint32_t> cig{cigel (50, BAM_CMATCH)};
    auto [first, second] =
        get_soft_clips (cig.data(), cig.size());
    CHECK (first == "");
    CHECK (second == "");
  }

  SECTION ("only first clipped")
  {
    std::vector<uint32_t> cig{
        cigel (7, BAM_CSOFT_CLIP), cigel (40, BAM_CMATCH)
    };
    auto [first, second] =
        get_soft_clips (cig.data(), cig.size());
    CHECK (first == "s(7)");
    CHECK (second == "");
  }

  SECTION ("only last clipped")
  {
    std::vector<uint32_t> cig{
        cigel (40, BAM_CMATCH), cigel (9, BAM_CSOFT_CLIP)
    };
    auto [first, second] =
        get_soft_clips (cig.data(), cig.size());
    CHECK (first == "");
    CHECK (second == "s(9)");
  }

  SECTION ("single op, fully soft-clipped")
  {
    std::vector<uint32_t> cig{cigel (12, BAM_CSOFT_CLIP)};
    auto [first, second] =
        get_soft_clips (cig.data(), cig.size());
    CHECK (first == "s(12)");
    CHECK (second == "s(12)");
  }

  SECTION ("empty cigar array is well-defined, not an OOB read")
  {
    std::vector<uint32_t> cig{};
    auto [first, second] =
        get_soft_clips (cig.data(), cig.size());
    CHECK (first == "");
    CHECK (second == "");
  }
}

TEST_CASE (
    "expand_sequence renders aligned display strings",
    "[stringify]"
)
{
  SECTION ("no ref: raw query bases pass through on match ops")
  {
    std::vector<uint32_t> cig{cigel (4, BAM_CMATCH)};
    auto out = expand_sequence (
        "ACGT", cig.data(), cig.size(), std::nullopt
    );
    CHECK (out == "ACGT");
  }

  SECTION (
      "with ref: matches masked as '=', mismatches keep query "
      "base"
  )
  {
    std::vector<uint32_t> cig{cigel (4, BAM_CMATCH)};
    ExpandSequenceRefArgs ref{
        .seqAlignStart = 0, .refSlice = "ACGA"
    };
    auto out = expand_sequence (
        "ACGT", cig.data(), cig.size(), std::make_optional (ref)
    );
    CHECK (out == "===T");
  }

  SECTION (
      "deletion op emits dashes and does not advance query "
      "offset"
  )
  {
    std::vector<uint32_t> cig{
        cigel (2, BAM_CMATCH), cigel (3, BAM_CDEL),
        cigel (2, BAM_CMATCH)
    };
    auto out = expand_sequence (
        "ACGT", cig.data(), cig.size(), std::nullopt
    );
    CHECK (out == "AC---GT");
  }

  SECTION (
      "insertion op advances query offset but emits nothing"
  )
  {
    std::vector<uint32_t> cig{
        cigel (2, BAM_CMATCH), cigel (3, BAM_CINS),
        cigel (2, BAM_CMATCH)
    };
    auto out = expand_sequence (
        "ACnnnGT", cig.data(), cig.size(), std::nullopt
    );
    CHECK (out == "ACGT");
  }

  SECTION (
      "soft clip op advances query offset but emits nothing"
  )
  {
    std::vector<uint32_t> cig{
        cigel (2, BAM_CSOFT_CLIP), cigel (4, BAM_CMATCH)
    };
    auto out = expand_sequence (
        "nnACGT", cig.data(), cig.size(), std::nullopt
    );
    CHECK (out == "ACGT");
  }

  SECTION (
      "ref offset stays correctly aligned across an intervening "
      "deletion"
  )
  {
    // M(2) D(3) M(2): query "ACGT" (4 bases, D contributes none),
    // ref   "AC???GT" -- the D block consumes 3 ref bases that the
    // query never sees, so the second M block must read ref starting
    // at offset 5, not 2.
    std::vector<uint32_t> cig{
        cigel (2, BAM_CMATCH), cigel (3, BAM_CDEL),
        cigel (2, BAM_CMATCH)
    };
    ExpandSequenceRefArgs ref{
        .seqAlignStart = 0, .refSlice = "ACxxxGT"
    };
    auto out = expand_sequence (
        "ACGT", cig.data(), cig.size(), std::make_optional (ref)
    );
    CHECK (out == "==---==");
  }
}
