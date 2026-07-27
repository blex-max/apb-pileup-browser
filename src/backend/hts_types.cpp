#include "backend/hts_types.hpp"

#include <fmt/format.h>
#include <htslib/faidx.h>

#include "backend/PileupDB.hpp"
#include "shared/err.hpp"

AlnOrErr load_aln (const char* fn)
{
  AlnFile aln;
  aln.o_fh = hts_open (fn, "r");
  if (!aln.o_fh) {
    return std::unexpected (make_htslib_err (
        -1,
        fmt::format ("Could not open alignment file at {}", fn)
    ));
  }
  aln.o_hdr = sam_hdr_read (aln.o_fh);
  if (!aln.o_hdr) {
    return std::unexpected (make_htslib_err (
        -1,
        "Could not read header "
        "from alignment file"
    ));
  }
  aln.o_idx = sam_index_load (aln.o_fh, fn);
  if (!aln.o_idx) {
    return std::unexpected (make_htslib_err (
        -1, fmt::format ("Could not load index for {}", fn)
    ));
  }

  return aln;
}

FastaOrErr load_fasta (const char* fn)
{
  FastaFile ff;

  ff.o_fai = fai_load3_format (
      fn, NULL, NULL, 0, fai_format_options::FAI_FASTA
  );

  if (!ff.o_fai) {
    return std::unexpected (make_htslib_err (
        -1, fmt::format ("Could not open fasta file at {}", fn)
    ));
  }

  return ff;
}

RefSliceOrErr fetch_region (
    const FastaFile& ff, const std::string_view contigName,
    hts_pos_t regStart, hts_pos_t regEnd
)
{
  hts_pos_t rc;
  auto p_fetch = faidx_fetch_seq64 (
      ff, contigName.data(), regStart, regEnd - 1, &rc
  );
  if (p_fetch == NULL) {
    std::string msg{"Could not retrieve region from fasta; "};
    if (rc == -2) {
      msg += fmt::format ("contig {} not found", contigName);
    }
    else {
      msg += "unspecified htslib error";
    }
    return std::unexpected (
        make_htslib_err (static_cast<int> (rc), msg)
    );
  }

  std::string out{p_fetch, static_cast<size_t> (rc)};

  free (p_fetch);

  return out;
}
