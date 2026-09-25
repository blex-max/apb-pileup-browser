#include "backend/hts_types.hpp"

#include <fmt/format.h>
#include <htslib/faidx.h>

#include "plog/Log.h"

std::expected<AlnFile, AlnFile::LoadErrCodes> AlnFile::load_aln (
    const std::string& path
)
{
  AlnFile aln;
  aln.o_fh = hts_open (path.c_str(), "r");
  if (aln.o_fh == nullptr) {
    return std::unexpected (AlnFile::LoadErrCodes::openFail);
  }
  aln.o_hdr = sam_hdr_read (aln.o_fh);
  if (aln.o_hdr == nullptr) {
    return std::unexpected (AlnFile::LoadErrCodes::hdrReadFail);
  }
  aln.o_idx = sam_index_load (aln.o_fh, path.c_str());
  if (aln.o_idx == nullptr) {
    return std::unexpected (
        AlnFile::LoadErrCodes::indexLoadFail
    );
  }

  return aln;
}

std::expected<FastaFile, FastaFile::LoadErrCodes>
FastaFile::load_fasta (const char* path)
{
  FastaFile ff;

  ff.o_fai = fai_load3_format (
      path, NULL, NULL, 0, fai_format_options::FAI_FASTA
  );

  if (ff.o_fai == nullptr) {
    return std::unexpected (FastaFile::LoadErrCodes::openFail);
  }

  return ff;
}

extern "C" {
int pileup_func (void* br_data, bam1_t* br_b)
{
  const PileupCapture* br_d = (PileupCapture*)(br_data);
  // No filtering
  return sam_itr_next (br_d->br_fh, br_d->o_it, br_b);
}
}
// Build pileup iterator at the specified locus
std::expected<PileupIterator, PileupIterator::ConstructErrCodes>
PileupIterator::prepare_pileup_iter (
    const AlnFile& aln, int32_t tid, hts_pos_t pos
)
{
  /*
    Creates new hts_itr_t each call since in this program there is
    at present only a single call to this function per program instance,
    and even in future there is no expected pattern to loci at which pileups
    might be needed, hence little advantage to keeping the iterator alive between calls.
  */
  PLOGD << "Begin prepare_pileup";
  PileupIterator out;

  PLOGD << "Initalising sam_itr_queryi";
  auto* o_alnIter =
      sam_itr_queryi (aln.o_idx, tid, pos, pos + 1);
  if (o_alnIter == NULL) {
    return std::unexpected (
        PileupIterator::ConstructErrCodes::samItrFail
    );
  }

  PLOGD << "Initialising bam_plp_t";
  out.o_cap = new PileupCapture{aln.o_fh, o_alnIter};
  auto* o_plp = bam_plp_init (pileup_func, out.o_cap);
  if (o_plp == NULL) {
    return std::unexpected (
        PileupIterator::ConstructErrCodes::pileupInitFail
    );
  }
  out.o_plp = o_plp;

  int64_t plpPos = -1;
  int plpTid = -1;
  int nPlp = -1;
  const bam_pileup1_t* br_plpArr;
  PLOGD << "Iterating pileup";
  while ((br_plpArr = bam_plp64_auto (
              out.o_plp, &plpTid, &plpPos, &nPlp
          )) != 0) {
    if (nPlp < 0 || plpTid < 0 || plpPos < 0) {
      return std::unexpected (
          PileupIterator::ConstructErrCodes::pileupIterateFail
      );
    }
    if (plpPos < pos) {
      continue;  // doesn't cover variant
    }
    PLOGD << "Position found";
    out.br_plpArr = br_plpArr;
    out.nPlp = static_cast<size_t> (nPlp);
    out.tid = tid;
    out.pos = pos;
    // fill span member
    out.span = GenomicSpan{INT64_MAX, 0};
    for (int i = 0; i < nPlp; i++) {
      auto* const b1 = br_plpArr[i].b;
      const auto rStart = b1->core.pos;
      const auto rEnd =
          rStart + bam_cigar2rlen (
                       static_cast<int> (b1->core.n_cigar),
                       bam_get_cigar (b1)
                   );
      out.span.start = std::min (out.span.start, rStart);
      out.span.end = std::max (out.span.end, rEnd);
    }
    return out;
  }
  return std::unexpected (
      PileupIterator::ConstructErrCodes::locusNotCovered
  );
}
