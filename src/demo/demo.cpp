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
  std::uniform_int_distribution<size_t> pick (0, 3);
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

VoidOrErr insert_demo_data (
    PileupDB& db, size_t regWidth, size_t nQuery,
    hts_pos_t gOffset
)
{
  const hts_pos_t pileupPos =
      static_cast<hts_pos_t> ((regWidth / 2) - 1);
  const auto qLen = static_cast<size_t> (pileupPos);
  auto refSeq = fixed_ref_seq (regWidth);
  refSeq[static_cast<size_t> (pileupPos)] =
      'A';  // known ref base at the variant site

  constexpr size_t maxDelLen = 4;

  // Headroom of maxDelLen reserved so start+qLen+delLen
  // can never exceed regWidth, whether or not a given read ends up with
  // a deletion.
  // Reserving start >= 1 keeps qPos in [0, qLen-1] for every read.
  std::uniform_int_distribution<size_t> gstartGen (
      1, qLen - maxDelLen
  );
  constexpr double mismatchRate = 0.01;
  std::bernoulli_distribution mismatchDist (mismatchRate);
  constexpr double pileupVaf = 0.30;
  std::bernoulli_distribution snvAlleleDist (pileupVaf);
  std::uniform_int_distribution<size_t> delLenGen (1, maxDelLen);
  constexpr size_t maxInsLen = 4;
  std::uniform_int_distribution<size_t> insLenGen (1, maxInsLen);
  std::uniform_int_distribution<uint8_t> mapQGen (0, 60);

  // At most one of {deletion, insertion, leading clip, trailing clip}
  // per read - eaiser to implement.
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

  std::vector<PileupFields> readsBuffer;
  readsBuffer.reserve (nQuery);
  GenomicSpan span{INT64_MAX, 0};
  std::mt19937 rng;
  for (size_t i = 0; i < nQuery; ++i) {
    PileupFields readI;
    readI.flag = 0;
    readI.isDel = false;
    readI.isRefSkip = false;
    readI.mapQ = mapQGen (rng);
    readI.mStart = -1;
    readI.mtidName = '*';  // not present
    readI.qName = "read" + std::to_string (i);

    readI.start = static_cast<hts_pos_t> (gstartGen (rng));
    const auto qPos =
        static_cast<int32_t> (pileupPos - readI.start);

    // Every variant below needs at least one base past the
    // pileup column to split/shrink the aligned run into.
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
        constexpr size_t maxClipLen = 20;
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

    const bool indelAtPileup =
        mSplit == static_cast<size_t> (qPos) + 1;
    readI.indel = indelAtPileup ? static_cast<int> (insLen) -
                                      static_cast<int> (delLen)
                                : 0;

    const auto finalQPos =
        leadClip ? qPos + static_cast<int32_t> (clipLen) : qPos;

    // Insertions add query bases that aren't in the reference, so
    // the read seq/qual buffers grow by insLen
    const size_t seqLen = qLen + insLen;
    std::string seq (seqLen, ' ');
    std::string qual (seqLen, ' ');
    std::array<char, 3> qualChars{'F', 'E', 'D'};
    std::discrete_distribution<uint8_t> qualCharPicker (
        {100, 20, 10}
    );
    for (size_t j = 0; j < seqLen; ++j) {
      qual[j] = qualChars[qualCharPicker (rng)];

      if (j == static_cast<size_t> (finalQPos)) {
        constexpr char pileupAlt = 'T';

        // fixed alt base at fixed VAF
        seq[j] = snvAlleleDist (rng)
                     ? pileupAlt
                     : refSeq[static_cast<size_t> (pileupPos)];
        continue;
      }

      const bool inClip =
          leadClip ? j < clipLen
                   : (clipLen > 0 && j >= qLen - clipLen);
      if (inClip) {
        seq[j] = random_base (rng);
        continue;
      }

      const bool inInsertion =
          insLen > 0 && j >= mSplit && j < mSplit + insLen;
      if (inInsertion) {
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
          static_cast<size_t> (readI.start) + alignedIdx +
          (alignedIdx < mSplit ? 0 : delLen);
      const char refBase = refSeq[refOffset];
      seq[j] = mismatchDist (rng) ? mutate_base (refBase, rng)
                                  : refBase;
    }
    readI.seqBases = std::move (seq);
    readI.qualAscii = std::move (qual);

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
    readI.nCig = cigOps.size();
    readI.rawCig = std::move (cigOps);
    readI.cig =
        stringify_cigar (readI.rawCig.data(), readI.nCig);

    readI.end =
        readI.start +
        (delLen > 0 ? static_cast<hts_pos_t> (qLen + delLen)
                    : static_cast<hts_pos_t> (qLen - clipLen));

    readI.qPos = finalQPos;
    readI.base = readI.seqBases[static_cast<size_t> (finalQPos)];
    readI.baseQual = static_cast<uint8_t> (
        readI.qualAscii[static_cast<size_t> (finalQPos)] - 33
    );
    readI.isHead = (finalQPos == 0);
    readI.isTail =
        (finalQPos == static_cast<int32_t> (qLen - 1));

    span.start = std::min (readI.start, span.start);
    span.end = std::max (readI.end, span.end);

    readsBuffer.push_back (std::move (readI));
  }

  std::sort (
      readsBuffer.begin(), readsBuffer.end(),
      [] (const PileupFields& a, const PileupFields& b) {
        return a.start < b.start;
      }
  );

  const auto refSlice = refSeq.substr (
      static_cast<size_t> (span.start),
      static_cast<size_t> (span.end - span.start)
  );

  for (auto& readI : readsBuffer) {
    readI.start += gOffset;
    readI.end += gOffset;
  }
  const hts_pos_t gPileupPos = pileupPos + gOffset;
  const GenomicSpan gSpan{
      span.start + gOffset, span.end + gOffset
  };

  auto imRet = insert_metadata (
      db, make_locus_data ("demo", gPileupPos, gSpan, refSlice)
  );
  if (!imRet) {
    return std::unexpected{imRet.error()};
  }

  auto stmtRet = prepare_insert_reads_stmt (db);
  if (!stmtRet) {
    return std::unexpected{stmtRet.error()};
  }
  auto stmt{std::move (*stmtRet)};

  if (auto beginRet = begin_transaction (db); !beginRet) {
    return std::unexpected{beginRet.error()};
  }

  for (const auto& readI : readsBuffer) {
    if (const int sqlRc = bind_pileup_fields (stmt, readI);
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
