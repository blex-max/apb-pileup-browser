#include "backend/hts_types.hpp"

#include <fmt/format.h>
#include <htslib/faidx.h>

#include "shared/err.hpp"

AlnOrErr load_aln (const char* br_fn)
{
  AlnFile aln;
  aln.o_fh = hts_open (br_fn, "r");
  if (aln.o_fh == nullptr) {
    return std::unexpected (make_htslib_err (
        -1, fmt::format (
                "Could not open alignment file at {}", br_fn
            )
    ));
  }
  aln.o_hdr = sam_hdr_read (aln.o_fh);
  if (aln.o_hdr == nullptr) {
    return std::unexpected (make_htslib_err (
        -1,
        "Could not read header "
        "from alignment file"
    ));
  }
  aln.o_idx = sam_index_load (aln.o_fh, br_fn);
  if (aln.o_idx == nullptr) {
    return std::unexpected (make_htslib_err (
        -1, fmt::format ("Could not load index for {}", br_fn)
    ));
  }

  return aln;
}

FastaOrErr load_fasta (const char* br_fn)
{
  FastaFile ff;

  ff.o_fai = fai_load3_format (
      br_fn, NULL, NULL, 0, fai_format_options::FAI_FASTA
  );

  if (ff.o_fai == nullptr) {
    return std::unexpected (make_htslib_err (
        -1,
        fmt::format ("Could not open fasta file at {}", br_fn)
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
  auto* o_fetch = faidx_fetch_seq64 (
      ff, contigName.data(), regStart, regEnd - 1, &rc
  );
  if (o_fetch == NULL) {
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

  std::string out{o_fetch, static_cast<size_t> (rc)};

  free (o_fetch);

  return out;
}
