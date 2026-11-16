#include "ingest.hpp"

#include <fmt/format.h>
#include <fmt/std.h>
#include <htslib/faidx.h>
#include <htslib/hts.h>
#include <htslib/sam.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "generic/describe.hpp"
#include "generic/transform_hts.hpp"
#include "model/apb_db.hpp"
#include "utility/apb_assert.hpp"
#include "utility/cleanup.hpp"

struct PileupCapture {
  htsFile* br_fh = nullptr;
  hts_itr_t* o_it = nullptr;  // owned; freed by PileupIterator::destroy
};
extern "C" {
int pileup_func (void* br_data, bam1_t* br_b)
{
  const PileupCapture* br_d = (PileupCapture*)(br_data);
  // No filtering
  return sam_itr_next (br_d->br_fh, br_d->o_it, br_b);
}
}

std::expected<void, std::string> ingest_sam_auto (
    sqlite3* conn, const std::vector<std::filesystem::path>& alnPaths,
    const std::string& locus,
    const std::optional<std::filesystem::path>& refPath, bool zeroBased,
    std::optional<size_t> loadLimitBytes,
    std::optional<size_t> loadLimitDepth
)
{
  // helper types for this fn
  struct AlnFile {
    htsFile* o_fh = nullptr;
    sam_hdr_t* o_hdr = nullptr;
    hts_idx_t* o_idx = nullptr;
    std::filesystem::path path;
    uint32_t locusTid = UINT32_MAX;

    void destroy() const noexcept
    {
      hts_idx_destroy (o_idx);
      sam_hdr_destroy (o_hdr);
      hts_close (o_fh);
    }
  };
  struct PileupIterator {
    PileupCapture* o_cap = nullptr;
    bam_plp_t o_plp = nullptr;
    const bam_pileup1_t* br_plpArr = nullptr;
    size_t nPlp = 0;

    void destroy() noexcept
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
  };

  // convert locus from string
  std::string locusContigName;
  hts_pos_t locusPos;
  const auto locusStrSepPos = locus.find_last_of (':');
  if (locusStrSepPos == 0) {
    return std::unexpected (
        "Invalid locus specifier; no contig before colon separator"
    );
  }
  if (locusStrSepPos == locus.length() - 1) {
    return std::unexpected (
        "Invalid locus specifier; no coordinate after colon separator"
    );
  }
  if (locusStrSepPos >= locus.length()) {
    return std::unexpected (
        "Invalid locus specifier; no colon separator present"
    );
  }
  if (std::all_of (
          begin (locus) + static_cast<long> (locusStrSepPos) + 1,
          locus.end(), isdigit
      )) {
    try {
      locusPos = std::stoll (locus.substr (locusStrSepPos + 1));
    }
    catch (const std::exception& ex) {
      return std::unexpected (
          fmt::format (
              "Invalid locus specifier; could not convert postion to "
              "64-bit integer ({})",
              ex.what()
          )
      );
    }
  }
  else {
    return std::unexpected (
        "Invalid locus specifier; non-digit [0123456789] character found "
        "in position after colon separator"
    );
  }
  if (locus[0] == '{' && locus[locusStrSepPos - 1] == '}') {
    // discard disambiguating braces
    locusContigName = locus.substr (1, locusStrSepPos - 2);
  }
  else {
    locusContigName = locus.substr (0, locusStrSepPos);
  }

  if (!zeroBased) {
    if (locusPos == 0) {
      return std::unexpected (
          "Invalid locus specifier for one-based input; position is less "
          "than 1"
      );
    }
    locusPos--;
  }

  // load reference if provided
  std::optional<faidx_t*> ff;
  if (refPath) {
    ff = fai_load3_format (
        refPath.value().c_str(), NULL, NULL, 0,
        fai_format_options::FAI_FASTA
    );

    if (ff == nullptr) {
      return std::unexpected (
          fmt::format ("Failed to open reference fasta at {}", *refPath)
      );
    }
  }
  Defer ff_cleanup ([&ff]() {
    if (ff) {
      fai_destroy (ff.value());
    }
  });

  // iterate alignment paths. Open, validate, pileup,
  // transform to db
  bool contigFound = false;
  bool readsFoundAtLocus = false;
  uint64_t estLocusBytes = 0;
  int64_t span0Start = INT64_MAX;
  int64_t span0End = 0;
  std::vector<apb_input_record1_t> inputRecBuf;
  if (const auto rc = sqlite3_exec (conn, "BEGIN;", NULL, NULL, NULL);
      rc != SQLITE_OK) {
    return std::unexpected (
        fmt::format (
            "Failed to begin database transaction, reporting: {} - {}",
            sqlite3_errstr (rc), sqlite3_errmsg (conn)
        )
    );
  }
  for (const auto& fp : alnPaths) {
    AlnFile aln;
    Defer aln_cleanup ([&aln]() { aln.destroy(); });

    aln.path = fp;
    aln.o_fh = hts_open (aln.path.c_str(), "r");
    if (aln.o_fh == nullptr) {
      return std::unexpected ("Failed to open alignment file");
    }
    aln.o_hdr = sam_hdr_read (aln.o_fh);
    if (aln.o_hdr == nullptr) {
      return std::unexpected (
          "Failed to read alignment file header; is it "
          "corrupt?"
      );
    }
    aln.o_idx = sam_index_load (aln.o_fh, aln.path.c_str());
    if (aln.o_idx == nullptr) {
      return std::unexpected (
          "Failed to load index file for alignment; is the "
          "file indexed?"
      );
    }

    bool alnHasContig = false;
    switch (const auto rc =
                sam_hdr_name2tid (aln.o_hdr, locusContigName.c_str());
            rc) {
    case -2:
      return std::unexpected (
          fmt::format (
              "Error while attempting to retrive contig id for contig "
              "{} from alignment file {}",
              locusContigName, aln.path
          )
      );
    case -1:
      break;
    default:
      alnHasContig = true;
      contigFound = true;
      aln.locusTid = static_cast<uint32_t> (rc);
    };

    PileupIterator pileup;
    Defer pileup_destroy ([&pileup]() { pileup.destroy(); });
    if (alnHasContig) {
      /*
        Creates new hts_itr_t each call since in this program there is
        at present only a single call to this function per program instance,
        and even in future there is no expected pattern to loci at which pileups
        might be needed, hence little advantage to keeping the iterator alive between calls.
      */

      auto* o_alnIter = sam_itr_queryi (
          aln.o_idx, static_cast<int> (aln.locusTid), locusPos,
          locusPos + 1
      );
      if (o_alnIter == NULL) {
        // FIXME: is this (and pileup iterator fail) actually plausible?
        return std::unexpected (
            fmt::format (
                "Failed to create read iterator for alignment at {}",
                aln.path
            )
        );
      }

      pileup.o_cap = new PileupCapture{aln.o_fh, o_alnIter};
      pileup.o_plp = bam_plp_init (pileup_func, pileup.o_cap);
      if (pileup.o_plp == NULL) {
        return std::unexpected (
            fmt::format (
                "Failed to create pileup iterator for alignment at {}",
                aln.path
            )
        );
      }
      bam_plp_set_maxcnt (
          pileup.o_plp, loadLimitDepth ? *loadLimitDepth : INT_MAX
      );

      int64_t plpPos = -1;
      int plpTid = -1;
      int nPlp = -1;
      const bam_pileup1_t* br_plpArr;
      while ((br_plpArr = bam_plp64_auto (
                  pileup.o_plp, &plpTid, &plpPos, &nPlp
              )) != 0) {
        if (nPlp < 0 || plpTid < 0 || plpPos < 0) {
          // FIXME: what is the error space here? should this be a crash or a skip. read src.
          return std::unexpected (
              fmt::format (
                  "Unexpected error encountered when iterating pileup for "
                  "alignment at {}",
                  aln.path
              )
          );
        }
        if (plpPos < locusPos) {
          continue;  // yet to reach locus
        }
        if (plpPos > locusPos) {
          break;
        }
        // else locus found (implying reads present)
        pileup.br_plpArr = br_plpArr;
        pileup.nPlp = static_cast<size_t> (nPlp);
        if (loadLimitDepth && pileup.nPlp >= *loadLimitDepth) {
          return std::unexpected (
              fmt::format (
                  "Pileup at locus exceeds {} reads in alignment file {}, "
                  "aborting. To uncap memory usage, use --no-limit-mem",
                  *loadLimitDepth, aln.path
              )
          );
        }
        for (int j = 0; j < nPlp; j++) {
          const auto* b1 = br_plpArr[j].b;
          if (loadLimitBytes) {
            estLocusBytes += sizeof (bam_pileup1_t) + sizeof (bam1_t) +
                             static_cast<unsigned long> (b1->l_data);
            if (estLocusBytes > *loadLimitBytes) {
              return std::unexpected (
                  fmt::format (
                      "Pileup at locus exceeds ~{}Gb in size, "
                      "aborting. To uncap memory usage, use "
                      "--no-limit-mem",
                      *loadLimitBytes / 1'000'000'000
                  )
              );
            }
          }
          const auto rStart = b1->core.pos;
          const auto rEnd =
              rStart +
              bam_cigar2rlen (
                  static_cast<int> (b1->core.n_cigar), bam_get_cigar (b1)
              );
          span0Start = std::min (span0Start, rStart);
          span0End = std::max (span0End, rEnd);
        }
        break;
      }
      // tid/pos are untouched if no column was ever yielded; only nPlp signals error
      if (nPlp < 0) {
        // FIXME: Again, what is the error space here? should this be a crash or a skip. read src.
        return std::unexpected (
            fmt::format (
                "Unexpected error encountered when iterating pileup for "
                "alignment at {}",
                aln.path
            )
        );
      }
    }

    int alnID;
    if (const auto ret =
            apb_insert_aln (conn, aln.path.string(), alnHasContig);
        !ret) {
      return std::unexpected (apb_describe_sqlite_failure (
          "Failed to insert alignment file", ret.error(),
          sqlite3_errmsg (conn)
      ));
    }
    else {
      alnID = *ret;
    }
    if (!alnHasContig || pileup.nPlp < 1) {
      continue;
    }
    readsFoundAtLocus = true;

    if (inputRecBuf.size() < pileup.nPlp) {
      // grow buffer
      inputRecBuf.resize (pileup.nPlp);
    }
    size_t iter_nrec = 0;
    /* transform and insert */
    if (const auto ret = apb_transform_pileup1 (
            {pileup.br_plpArr, pileup.nPlp}, inputRecBuf,
            [&aln] (int mtid) {
              return sam_hdr_tid2name (aln.o_hdr, mtid);
            }
        );
        !ret) {
      const auto& err = ret.error();
      switch (err.code) {
      case transform_p1_err_t::aux_parse_fail:
        return std::unexpected (
            fmt::format (
                "Unspecified failure in htslib when attempting to parse "
                "aux tag in read {}. Is tag data corrupt?",
                bam_get_qname (err.b)
            )
        );
      }
    }
    else {
      iter_nrec = *ret;
    }
    APB_ASSERT (iter_nrec == pileup.nPlp, iter_nrec, pileup.nPlp);
    if (const auto rc = apb_insert_records (
            conn, {inputRecBuf.data(), iter_nrec}, alnID
        );
        rc != SQLITE_OK) {
      return std::unexpected (apb_describe_sqlite_failure (
          "Failed to transform/insert alignment data", rc,
          sqlite3_errmsg (conn)
      ));
    }
  }

  if (!contigFound) {
    return std::unexpected (
        "Contig not found in any input alignment file"
    );
  }

  if (readsFoundAtLocus) {
    // Fetch reference and insert metadata
    std::optional<std::string> refSlice;
    if (ff) {
      hts_pos_t regLen;
      // uses closed coordinates, hence -1
      auto* o_fetch = faidx_fetch_seq64 (
          *ff, locusContigName.c_str(), span0Start, span0End - 1, &regLen
      );
      if (o_fetch == NULL) {
        return std::unexpected (
            fmt::format (
                "Failed to fetch reference region from fasta at {} "
                "for span {}:{}-{}",
                refPath.value(), locusContigName, span0Start, span0End
            )
        );
      }
      if (regLen < (span0End - span0Start)) {
        return std::unexpected (
            "Reference too short! Fasta missing coverage for pileup span."
        );
      }
      refSlice = {o_fetch, static_cast<size_t> (regLen)};
      free (o_fetch);
    }

    if (const auto rc = apb_insert_locus (
            conn, locusContigName, locusPos, span0Start, span0End, refSlice
        );
        rc != SQLITE_OK) {
      return std::unexpected (apb_describe_sqlite_failure (
          "Failed to insert locus metadata", rc, sqlite3_errmsg (conn)
      ));
    }
  }
  else {
    return std::unexpected (
        "No reads at locus in any input alignment file"
    );
  }

  if (const auto rc = sqlite3_exec (conn, "COMMIT;", NULL, NULL, NULL);
      rc != SQLITE_OK) {
    return std::unexpected (
        fmt::format (
            "Failed to commit database transaction, reporting: {} - {}",
            sqlite3_errstr (rc), sqlite3_errmsg (conn)
        )
    );
  }

  // FIXME: incomplete - must finally call (not yet existing) db verifaction method
  return {};
}

/* demo */
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

void ingest_demo (sqlite3* conn)
{
  APB_ASSERT (conn != nullptr);

  constexpr std::string_view demoContig = "demo-contig";
  constexpr int64_t pos = 10'000'000;
  constexpr size_t regWidth = 300;
  constexpr size_t nAln = 3;
  constexpr size_t nQueryPerAln = 300;

  // every read has a core aligned run of this length, covering the
  // pileup column.
  constexpr size_t readLen = (regWidth / 2) - 1;
  auto refSeq = fixed_ref_seq (regWidth);
  // refSeq[0] sits at this absolute position; pos is refSeq[readLen]
  constexpr int64_t winStart = pos - static_cast<int64_t> (readLen);
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
  const std::array<uint8_t, 5> qualVals{
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
  std::uniform_int_distribution<int64_t> mateDistGen (100, 400);
  // at most one of none/dup/secondary/supplementary/qcfail
  constexpr std::array<uint16_t, 5> filterFlags{
      0, BAM_FDUP, BAM_FSECONDARY, BAM_FSUPPLEMENTARY, BAM_FQCFAIL
  };
  std::discrete_distribution<int> filterPicker ({87, 8, 2, 2, 1});

  // read/ref coordinates are absolute (0-based); index refSeq via winStart.
  // every read covers pos.
  int64_t spanStart_0ho = pos;
  int64_t spanEnd_0ho = pos + 1;
  std::mt19937 rng;
  // one record set per alignment file
  std::array<std::vector<apb_input_record1_t>, nAln> alns;
  for (size_t i = 0; i < nAln * nQueryPerAln; ++i) {
    const size_t alnIdx = i / nQueryPerAln;
    auto& rec = alns[alnIdx].emplace_back();

    rec.start_0ho = winStart + static_cast<int64_t> (gstartGen (rng));
    // offset of the pileup column within the core aligned run
    const auto colOff = static_cast<size_t> (pos - rec.start_0ho);

    const auto pushOp = [&rec] (size_t len, int op) {
      const auto cigel = static_cast<uint32_t> (bam_cigar_gen (len, op));
      rec.rawCig.push_back (cigel);
      rec.cig += std::to_string (len);
      rec.cig += bam_cigar_opchr (cigel);
    };
    const auto allele = static_cast<Allele> (allelePicker (rng));
    const auto feature =
        static_cast<AlignmentFeature> (featurePicker (rng));

    if (feature == AlignmentFeature::LeadClip) {
      pushOp (clipLenGen (rng), BAM_CSOFT_CLIP);
    }
    rec.indel = 0;  // meaning per htslib p1.indel
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
      rec.indel = isDel ? -static_cast<int> (n) : static_cast<int> (n);
    }
    else {
      pushOp (readLen, BAM_CMATCH);
    }
    if (feature == AlignmentFeature::TailClip) {
      pushOp (clipLenGen (rng), BAM_CSOFT_CLIP);
    }
    rec.nCig = rec.rawCig.size();

    // Walk the CIGAR to build the read sequence, tracking ref and query
    // cursors. The pileup column is inside an M op, or a D op for Del.
    std::string seq;
    int64_t nm = 0;  // edit distance, for the NM tag
    auto refPos = rec.start_0ho;
    for (const auto cig : rec.rawCig) {
      const size_t len = bam_cigar_oplen (cig);
      switch (bam_cigar_op (cig)) {
      case BAM_CMATCH:
        for (size_t k = 0; k < len; ++k, ++refPos) {
          const char refBase =
              refSeq[static_cast<size_t> (refPos - winStart)];
          char b;
          if (refPos == pos) {
            rec.qPos = static_cast<int32_t> (seq.size());
            b = allele == Allele::Alt ? pileupAlt : refBase;
          }
          else {
            b = mismatchDist (rng) ? mutate_base (refBase, rng) : refBase;
          }
          if (b != refBase) {
            ++nm;
          }
          seq += b;
        }
        break;
      case BAM_CDEL:
        if (pos >= refPos && pos < refPos + static_cast<int64_t> (len)) {
          // htslib: qpos of a deleted column is the next query base
          rec.qPos = static_cast<int32_t> (seq.size());
          rec.isDel = true;
        }
        refPos += static_cast<int64_t> (len);
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
    APB_ASSERT (rec.qPos >= 0);
    rec.end_0ho = refPos;

    std::vector<uint8_t> qual (seq.size());
    for (auto& q : qual) {
      q = qualVals[static_cast<size_t> (qualValPicker (rng))];
    }

    const auto qPos = static_cast<size_t> (rec.qPos);
    rec.isHead = qPos == 0;
    rec.isTail = qPos == seq.size() - 1;
    rec.isRefSkip = false;  // not in demo
    if (!rec.isDel) {
      rec.base = seq[qPos];
      rec.baseQual = qual[qPos];
    }
    rec.seq = std::move (seq);
    rec.rawQual = std::move (qual);

    uint16_t flag = BAM_FPAIRED;
    flag |= coinFlip (rng) ? BAM_FREAD1 : BAM_FREAD2;
    flag |= filterFlags[static_cast<size_t> (filterPicker (rng))];
    const bool isRev = coinFlip (rng);
    if (isRev) {
      flag |= BAM_FREVERSE;
    }
    // unmapped mate sits at this read's position, per SAM recommended
    // practice (bwa, minimap2, fixmate, htsjdk)
    rec.mStart = rec.start_0ho;
    if (mateUnmappedDist (rng)) {
      flag |= BAM_FMUNMAP;
    }
    else {
      flag |= BAM_FPROPER_PAIR;
      // FR orientation: mate on the opposite strand, facing this read
      const auto d = mateDistGen (rng);
      rec.mStart = isRev ? rec.start_0ho - d : rec.start_0ho + d;
      if (!isRev) {
        flag |= BAM_FMREVERSE;
      }
    }
    rec.flag = flag;
    rec.mtidName = "=";  // single demo contig

    rec.mapQ = 60;
    switch (mapQBand (rng)) {
    case 0:
      rec.mapQ = 0;
      break;
    case 1:
      rec.mapQ = static_cast<uint8_t> (mapQMid (rng));
      break;
    default:
      break;
    }

    rec.qName = "read" + std::to_string (i);
    // one read group per alignment file
    rec.auxJson =
        fmt::format (R"({{"NM":{},"RG":"sample{}"}})", nm, alnIdx + 1);

    spanStart_0ho = std::min (rec.start_0ho, spanStart_0ho);
    spanEnd_0ho = std::max (rec.end_0ho, spanEnd_0ho);
  }

  APB_ASSERT (
      const auto rc = sqlite3_exec (conn, "BEGIN;", NULL, NULL, NULL);
      rc == SQLITE_OK, rc, sqlite3_errstr (rc), sqlite3_errmsg (conn)
  );
  for (size_t i = 0; i < alns.size(); ++i) {
    auto& aln = alns[i];
    std::sort (begin (aln), end (aln), [] (const auto& a, const auto& b) {
      return a.start_0ho < b.start_0ho;
    });
    std::string alnPath{"/data/samples/PD12345"};
    alnPath += static_cast<char> ('a' + i);
    alnPath.append (".bam");

    int alnId;
    if (const auto ret = apb_insert_aln (conn, alnPath, true); !ret) {
      APB_UNREACHABLE (
          "demo insertion malformed", sqlite3_errstr (ret.error()),
          sqlite3_errmsg (conn)
      );
    }
    else {
      alnId = *ret;
    }

    if (const auto rc = apb_insert_records (conn, aln, alnId);
        rc != SQLITE_OK) {
      APB_UNREACHABLE (
          "demo insertion malformed", sqlite3_errstr (rc),
          sqlite3_errmsg (conn)
      );
    }
  }

  const auto ref = refSeq.substr (
      static_cast<size_t> (spanStart_0ho - winStart),
      static_cast<size_t> (spanEnd_0ho - spanStart_0ho)
  );
  APB_ASSERT (
      const auto rc = apb_insert_locus (
          conn, std::string{demoContig}, pos, spanStart_0ho, spanEnd_0ho,
          ref
      );
      rc == SQLITE_OK, sqlite3_errstr (rc), sqlite3_errmsg (conn)
  );
  APB_ASSERT (
      const auto rc = sqlite3_exec (conn, "COMMIT;", NULL, NULL, NULL);
      rc == SQLITE_OK, rc, sqlite3_errstr (rc), sqlite3_errmsg (conn)
  );
}
