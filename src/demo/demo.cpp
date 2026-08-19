#include "demo.hpp"

#include <htslib/sam.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <random>
#include <string>

#include "backend/PileupDB.hpp"
#include "backend/pileup_ingest.hpp"
#include "shared/err.hpp"

static const char sh_bases[] = "ACGT";

// Deterministic reference sequence
static std::string fixed_ref_seq (size_t len)
{
  std::string out;
  out.reserve (len);
  for (size_t i = 0; i < len; ++i) {
    out += sh_bases[i % 4];
  }
  return out;
}

static char random_base (std::mt19937& rng)
{
  std::uniform_int_distribution<size_t> pick (0, 3);
  return sh_bases[pick (rng)];
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

VoidOrErr insert_demo_data (
    PileupDB& db, size_t regWidth, size_t nQuery
)
{
  std::mt19937 rng;

  const hts_pos_t pileupPos =
      static_cast<hts_pos_t> ((regWidth / 2) - 1);
  const auto qLen = static_cast<size_t> (pileupPos);
  auto refSeq = fixed_ref_seq (regWidth);
  refSeq[static_cast<size_t> (pileupPos)] =
      'A';  // known ref base at the variant site

  constexpr double mismatchRate = 0.01;
  constexpr size_t maxDelLen = 4;
  constexpr size_t maxInsLen = 4;
  constexpr size_t maxClipLen = 20;
  constexpr char pileupAlt = 'T';
  constexpr double pileupVaf = 0.30;

  // Headroom of maxDelLen reserved unconditionally so start+qLen+delLen
  // can never exceed regWidth, whether or not a given read ends up with
  // a deletion. Clips only ever shrink a read's ref-consumed length, so
  // they need no extra headroom here. Insertions likewise need none: an
  // insertion adds query bases, not reference-consumed ones, so it never
  // grows the ref span a read spans.
  //
  // Lower bound is 1, not 0: pileupPos is numerically equal to qLen (both
  // derived from (regWidth/2)-1), so a read starting at exactly 0 would
  // get qPos == qLen -- one past the end of its own qLen-length
  // seqBases/qualAscii. Reserving start >= 1 keeps qPos in [0, qLen-1]
  // for every read.
  std::uniform_int_distribution<size_t> gstartGen (
      1, qLen - maxDelLen
  );
  std::bernoulli_distribution mismatchDist (mismatchRate);
  std::bernoulli_distribution snvAlleleDist (pileupVaf);
  std::uniform_int_distribution<size_t> delLenGen (1, maxDelLen);
  std::uniform_int_distribution<size_t> insLenGen (1, maxInsLen);

  // At most one of {deletion, insertion, leading clip, trailing clip}
  // per read -- keeps CIGAR/index math to a handful of cases instead of
  // a combinatorial explosion. Weights are just "occasional variety",
  // tunable.
  enum class ReadVariant : uint8_t {
    None,
    Deletion,
    LeadClip,
    TailClip,
    Insertion
  };
  std::discrete_distribution<int> variantDist (
      {0.55, 0.15, 0.10, 0.10, 0.10}
  );

  // Generate all reads' fields up front (no DB calls yet), tracking the
  // overall span so the loci row -- inserted below, before any reads
  // that FK-reference it -- can carry real pos/start/end/refSlice
  // instead of a placeholder.
  std::vector<PileupFields> reads;
  reads.reserve (nQuery);
  GenomicSpan span{INT64_MAX, 0};

  for (size_t i = 0; i < nQuery; ++i) {
    PileupFields ru_pf;
    ru_pf.flag = 0;
    ru_pf.isDel = false;
    ru_pf.isRefSkip = false;
    ru_pf.mapQ = 30;
    ru_pf.mStart = -1;
    ru_pf.qName = "read" + std::to_string (i);

    ru_pf.start = static_cast<hts_pos_t> (gstartGen (rng));
    const auto qPos =
        static_cast<int32_t> (pileupPos - ru_pf.start);

    // Every variant below needs at least one base of "room" past the
    // pileup column to split/shrink the aligned run into -- same guard
    // for all three, so qPos/isHead/isTail stay exactly the plain-read
    // formulas below regardless of which variant (if any) got picked;
    // only what's generated on either side of the pileup column changes.
    const bool hasRoom = qPos <= static_cast<int32_t> (qLen) - 2;
    const auto variant =
        hasRoom ? static_cast<ReadVariant> (variantDist (rng))
                : ReadVariant::None;

    size_t delLen = 0;
    size_t insLen = 0;
    size_t mSplit = qLen;
    size_t clipLen = 0;
    const bool leadClip = (variant == ReadVariant::LeadClip);

    switch (variant) {
      case ReadVariant::Deletion:
      case ReadVariant::Insertion: {
        std::uniform_int_distribution<size_t> splitGen (
            static_cast<size_t> (qPos) + 1, qLen - 1
        );
        mSplit = splitGen (rng);
        if (variant == ReadVariant::Deletion) {
          delLen = delLenGen (rng);
        }
        else {
          insLen = insLenGen (rng);
        }
        break;
      }
      case ReadVariant::LeadClip:
      case ReadVariant::TailClip: {
        const size_t maxClip = std::min (
            maxClipLen, qLen - 1 - static_cast<size_t> (qPos)
        );
        std::uniform_int_distribution<size_t> clipGen (
            1, maxClip
        );
        clipLen = clipGen (rng);
        break;
      }
      case ReadVariant::None:
        break;
    }

    // indel is only nonzero when the event immediately follows the
    // pileup base in THIS read (htslib bam_pileup1_t::indel semantics)
    // -- not merely "this read contains an indel somewhere".
    const bool indelAtPileup =
        mSplit == static_cast<size_t> (qPos) + 1;
    ru_pf.indel = indelAtPileup ? static_cast<int> (insLen) -
                                      static_cast<int> (delLen)
                                : 0;

    const auto finalQPos =
        leadClip ? qPos + static_cast<int32_t> (clipLen) : qPos;

    // Insertions add query bases that aren't in the reference, so
    // (unlike deletions, which only widen the ref span) the read's own
    // seq/qual buffers grow by insLen; insLen is 0 for every other
    // variant, so this is a no-op there.
    const size_t seqLen = qLen + insLen;
    std::string seq (seqLen, ' ');
    std::string qual (seqLen, ' ');
    for (size_t j = 0; j < seqLen; ++j) {
      qual[j] = 'F';

      if (j == static_cast<size_t> (finalQPos)) {
        // Designed SNV at the pileup locus: a fixed alt base at a fixed
        // VAF, distinct from (and not diluted by) the generic background
        // mismatch roll below.
        seq[j] = snvAlleleDist (rng)
                     ? pileupAlt
                     : refSeq[static_cast<size_t> (pileupPos)];
        continue;
      }

      const bool inClip =
          leadClip ? j < clipLen
                   : (clipLen > 0 && j >= qLen - clipLen);
      if (inClip) {
        // Clipped bases aren't aligned to any reference position --
        // nothing to compare against, so they're plain random filler.
        seq[j] = random_base (rng);
        continue;
      }

      const bool inInsertion =
          insLen > 0 && j >= mSplit && j < mSplit + insLen;
      if (inInsertion) {
        // Inserted bases aren't aligned to any reference position
        // either -- same treatment as clipped bases.
        seq[j] = random_base (rng);
        continue;
      }

      size_t alignedIdx = j;
      if (leadClip) {
        alignedIdx = j - clipLen;
      }
      else if (insLen > 0 && j >= mSplit + insLen) {
        alignedIdx = j - insLen;
      }
      const size_t refOffset =
          static_cast<size_t> (ru_pf.start) + alignedIdx +
          (alignedIdx < mSplit ? 0 : delLen);
      const char refBase = refSeq[refOffset];
      seq[j] = mismatchDist (rng) ? mutate_base (refBase, rng)
                                  : refBase;
    }
    ru_pf.seqBases = std::move (seq);
    ru_pf.qualAscii = std::move (qual);

    std::vector<uint32_t> cigOps;
    if (leadClip) {
      cigOps.push_back (
          static_cast<uint32_t> (
              bam_cigar_gen (clipLen, BAM_CSOFT_CLIP)
          )
      );
    }
    if (delLen > 0) {
      cigOps.push_back (
          static_cast<uint32_t> (
              bam_cigar_gen (mSplit, BAM_CMATCH)
          )
      );
      cigOps.push_back (
          static_cast<uint32_t> (
              bam_cigar_gen (delLen, BAM_CDEL)
          )
      );
      cigOps.push_back (
          static_cast<uint32_t> (
              bam_cigar_gen (qLen - mSplit, BAM_CMATCH)
          )
      );
    }
    else if (insLen > 0) {
      cigOps.push_back (
          static_cast<uint32_t> (
              bam_cigar_gen (mSplit, BAM_CMATCH)
          )
      );
      cigOps.push_back (
          static_cast<uint32_t> (
              bam_cigar_gen (insLen, BAM_CINS)
          )
      );
      cigOps.push_back (
          static_cast<uint32_t> (
              bam_cigar_gen (qLen - mSplit, BAM_CMATCH)
          )
      );
    }
    else {
      cigOps.push_back (
          static_cast<uint32_t> (
              bam_cigar_gen (qLen - clipLen, BAM_CMATCH)
          )
      );
    }
    if (!leadClip && clipLen > 0) {
      cigOps.push_back (
          static_cast<uint32_t> (
              bam_cigar_gen (clipLen, BAM_CSOFT_CLIP)
          )
      );
    }
    ru_pf.nCig = cigOps.size();
    ru_pf.rawCig = std::move (cigOps);
    ru_pf.cig =
        stringify_cigar (ru_pf.rawCig.data(), ru_pf.nCig);

    ru_pf.end =
        ru_pf.start +
        (delLen > 0 ? static_cast<hts_pos_t> (qLen + delLen)
                    : static_cast<hts_pos_t> (qLen - clipLen));

    ru_pf.qPos = finalQPos;
    ru_pf.base = ru_pf.seqBases[static_cast<size_t> (finalQPos)];
    ru_pf.baseQual = static_cast<uint8_t> (
        ru_pf.qualAscii[static_cast<size_t> (finalQPos)] - 33
    );
    ru_pf.isHead = (finalQPos == 0);
    ru_pf.isTail =
        (finalQPos == static_cast<int32_t> (qLen - 1));

    span.start = std::min (ru_pf.start, span.start);
    span.end = std::max (ru_pf.end, span.end);

    reads.push_back (std::move (ru_pf));
  }

  std::sort (
      reads.begin(), reads.end(),
      [] (const PileupFields& a, const PileupFields& b) {
        return a.start < b.start;
      }
  );

  const auto refSlice = refSeq.substr (
      static_cast<size_t> (span.start),
      static_cast<size_t> (span.end - span.start)
  );

  // demo data has no real alignment file / contigs; placeholder
  // metadata row just satisfies the reads table's loci_id FK chain.
  const AlnFile dummyAln;
  auto imRet = insert_metadata (db, dummyAln);
  if (!imRet) {
    return std::unexpected{imRet.error()};
  }

  auto ilRet = insert_loci (
      db, make_locus_data ("demo", pileupPos, span, refSlice)
  );
  if (!ilRet) {
    return std::unexpected{ilRet.error()};
  }
  const int lociId = *ilRet;

  auto stmtRet = prepare_insert_reads_stmt (db);
  if (!stmtRet) {
    return std::unexpected{stmtRet.error()};
  }
  auto stmt{std::move (*stmtRet)};

  if (auto beginRet = begin_transaction (db); !beginRet) {
    return std::unexpected{beginRet.error()};
  }

  for (const auto& ru_pf : reads) {
    if (const int sqlRc =
            bind_pileup_fields (stmt, lociId, ru_pf);
        sqlRc != SQLITE_OK) {
      Err err = make_sqlite3_err (sqlRc, sqlite3_errmsg (db));
      rollback_on_err (db, err);
      return std::unexpected{err};
    }

    if (const int sqlRc = sqlite3_step (stmt);
        sqlRc != SQLITE_DONE) {
      Err err = make_sqlite3_err (sqlRc, sqlite3_errmsg (db));
      rollback_on_err (db, err);
      return std::unexpected{err};
    }
    sqlite3_reset (
        stmt
    );  // rc mirrors the step already checked above
    sqlite3_clear_bindings (
        stmt
    );  // cannot fail per sqlite3 docs
  }

  auto comRet = commit (db);
  if (!comRet) {
    return std::unexpected{comRet.error()};
  }

  return {};
}
