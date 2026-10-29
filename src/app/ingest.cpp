#include "ingest.hpp"

#include <fmt/format.h>
#include <fmt/std.h>
#include <htslib/faidx.h>
#include <htslib/hts.h>
#include <htslib/sam.h>

#include "generic/demo.hpp"
#include "generic/describe.hpp"
#include "model/hts2apb.hpp"
#include "model/hts_types.hpp"
#include "shared/apb_assert.hpp"
#include "shared/cleanup.hpp"
#include "shared/stringify.hpp"

/* bounds */
// max pileup size, to prevent accidentally loading
// some RAM-destroying pileup.
#define DEPTH_LIMIT 500'000
#define GB_LIMIT 4
constexpr auto kBytesLimit =
    static_cast<uint64_t> (GB_LIMIT) * 1'000'000'000;

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
    bool limitMemUsage
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
      bam_plp_destroy (o_plp);
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
  genomic_span_t maxSpan{
      INT64_MAX, 0
  };  // for fetching appropriate reference slice
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
        APB_UNREACHABLE ("Could not parse header despite successful read");
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
          pileup.o_plp, limitMemUsage ? DEPTH_LIMIT : INT_MAX
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
        if (limitMemUsage && pileup.nPlp >= DEPTH_LIMIT) {
          return std::unexpected (
              fmt::format (
                  "Pileup at locus exceeds {} reads in alignment file {}, "
                  "aborting. To uncap memory usage, use --no-limit-mem",
                  DEPTH_LIMIT, aln.path
              )
          );
        }
        for (int j = 0; j < nPlp; j++) {
          const auto* b1 = br_plpArr[j].b;
          if (limitMemUsage) {
            estLocusBytes += sizeof (bam_pileup1_t) + sizeof (bam1_t) +
                             static_cast<unsigned long> (b1->l_data);
            if (estLocusBytes > kBytesLimit) {
              return std::unexpected (
                  "Pileup at locus exceeds ~" STR (
                      GB_LIMIT
                  ) "Gb in size, "
                    "aborting. To uncap memory usage, use --no-limit-mem"
              );
            }
          }
          const auto rStart = b1->core.pos;
          const auto rEnd =
              rStart +
              bam_cigar2rlen (
                  static_cast<int> (b1->core.n_cigar), bam_get_cigar (b1)
              );
          maxSpan.start = std::min (maxSpan.start, rStart);
          maxSpan.end = std::max (maxSpan.end, rEnd);
        }
        break;
      }
    }

    int alnID;
    if (const auto ret = apb_insert_aln (
            conn, aln.path.string(), alnHasContig,
            static_cast<int64_t> (pileup.nPlp)
        );
        !ret) {
      APB_UNREACHABLE (
          "failed to insert alignment", ret.error(),
          sqlite3_errstr (ret.error()), sqlite3_errmsg (conn)
      );
    }
    else {
      alnID = *ret;
    }
    if (!alnHasContig || pileup.nPlp < 1) {
      continue;
    }
    readsFoundAtLocus = true;

    const auto get_mtid_name = [&aln] (int mtid) {
      return sam_hdr_tid2name (aln.o_hdr, mtid);
    };
    if (const auto ret = apb_insert_pileup (
            conn, {pileup.br_plpArr, pileup.nPlp}, alnID, get_mtid_name
        );
        !ret) {
      const auto& err = ret.error();
      switch (err.kind) {
        case insert_pileup_err_t::sqlFail:
          return std::unexpected (apb_describe_sqlite_failure (
              "Failed to transform/insert alignment data", err.rc.value(),
              sqlite3_errmsg (conn)
          ));
        case insert_pileup_err_t::auxParseFail:
          // FIXME: provide qname/read/tag details
          return std::unexpected (
              "Failed to parse aux tag in alignment file. Is aux "
              "data corrupt?"
          );
        default:
          APB_UNREACHABLE ("unrecognised InsertPileupErr code");
      }
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
          *ff, locusContigName.c_str(), maxSpan.start, maxSpan.end - 1,
          &regLen
      );
      if (o_fetch == NULL) {
        return std::unexpected (
            fmt::format (
                "Failed to fetch reference region from fasta at {} "
                "for span {}:{}-{} (0-based end-exclusive coordinates)",
                refPath.value(), locusContigName, maxSpan.start,
                maxSpan.end
            )
        );
      }
      refSlice = {o_fetch, static_cast<size_t> (regLen)};
      free (o_fetch);
    }

    if (const auto rc = apb_insert_locus (
            conn, locusContigName, locusPos, maxSpan, refSlice
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

  // FIXME: incomplete - must finally call (not yet existing) db verifaction method
  return {};
}

void ingest_demo_auto (sqlite3* conn)
{
  APB_ASSERT (conn != nullptr);

  constexpr std::string_view demoContig = "demo-contig";
  constexpr hts_pos_t demoPos = 10'000'000;

  auto demoData = generate_demo_data (300, 300, demoPos);
  Defer data_cleanup ([&demoData]() { demoData.destroy(); });
  std::array<std::span<bam_pileup1_t>, 3> demoAlns{
      std::span<bam_pileup1_t>{
          begin (demoData.p), begin (demoData.p) + 100
      },
      std::span<bam_pileup1_t>{
          begin (demoData.p) + 100, begin (demoData.p) + 200
      },
      std::span<bam_pileup1_t>{begin (demoData.p) + 200, end (demoData.p)}
  };
  for (size_t i = 0; i < demoAlns.size(); ++i) {
    const auto plp = demoAlns[i];
    std::string alnPath{"/data/samples/demo-aln-"};
    alnPath.append (std::to_string (i));

    int alnId;
    if (const auto ret = apb_insert_aln (
            conn, alnPath, true, static_cast<int64_t> (plp.size())
        );
        !ret) {
      APB_UNREACHABLE (
          "demo insertion malformed", sqlite3_errstr (ret.error()),
          sqlite3_errmsg (conn)
      );
    }
    else {
      alnId = *ret;
    }

    if (const auto ret = apb_insert_pileup (
            conn, plp, alnId,
            [&demoContig] (int) { return demoContig.data(); }
        );
        !ret) {
      const auto& err = ret.error();
      switch (err.kind) {
        case insert_pileup_err_t::sqlFail:
          APB_UNREACHABLE (
              "demo insertion malformed", sqlite3_errstr (err.rc.value()),
              sqlite3_errmsg (conn)
          );
        case insert_pileup_err_t::auxParseFail:
          APB_UNREACHABLE ("bad tag data in demo pileup");
        default:
          APB_UNREACHABLE ("unrecognised InsertPileupErr code");
      }
    }
  }
  APB_ASSERT (
      const auto rc = apb_insert_locus (
          conn, std::string{demoContig}, demoPos, demoData.pspan,
          demoData.ref
      );
      rc == SQLITE_OK, rc, sqlite3_errstr (rc), sqlite3_errmsg (conn)
  );
}
