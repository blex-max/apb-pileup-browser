#include "demo.hpp"

#include <htslib/sam.h>

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>

#include "backend/PileupDB.hpp"
#include "backend/pileup_ingest.hpp"
#include "shared/err.hpp"

static std::string random_base_seq (size_t len)
{
  static const char bases[] = "ATCG";
  std::string out;
  for (size_t i = 0; i < len; ++i) {
    out += bases
        [static_cast<size_t> (rand()) % (sizeof (bases) - 1)];
  }
  return out;
}

// A base guaranteed to differ from refBase, for injecting mismatches
// into otherwise ref-copied synthetic reads.
static char mutate_base (char refBase, std::mt19937& rng)
{
  static const char bases[] = "ATCG";
  std::uniform_int_distribution<size_t> pick (0, 3);
  char b;
  do {
    b = bases[pick (rng)];
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
  const auto refSeq = random_base_seq (regWidth);

  constexpr double kMismatchRate = 0.02;
  constexpr double kDeletionRate = 0.15;
  constexpr size_t kMaxDelLen = 4;
  constexpr int kMinQual = 20;
  constexpr int kMaxQual = 40;

  // Headroom of kMaxDelLen reserved unconditionally so start+qLen+delLen
  // can never exceed regWidth, whether or not a given read ends up with
  // a deletion.
  std::uniform_int_distribution<size_t> gstartGen (
      0, qLen - kMaxDelLen
  );
  std::bernoulli_distribution mismatchDist (kMismatchRate);
  std::bernoulli_distribution deletionDist (kDeletionRate);
  std::uniform_int_distribution<size_t> delLenGen (
      1, kMaxDelLen
  );
  std::uniform_int_distribution<int> qualGen (
      kMinQual, kMaxQual
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
    ru_pf.indel = 0;
    ru_pf.isDel = false;
    ru_pf.isRefSkip = false;
    ru_pf.mapQ = 30;
    ru_pf.mStart = -1;
    ru_pf.qName = "read" + std::to_string (i);

    ru_pf.start = static_cast<hts_pos_t> (gstartGen (rng));
    const auto qPos =
        static_cast<int32_t> (pileupPos - ru_pf.start);

    // Occasionally splice a short deletion strictly after the pileup
    // column, so qPos/isHead/isTail below stay exactly as in the
    // no-deletion case -- only what's generated past the split point
    // (mSplit) changes.
    size_t delLen = 0;
    size_t mSplit = qLen;
    if (qPos <= static_cast<int32_t> (qLen) - 2 &&
        deletionDist (rng)) {
      std::uniform_int_distribution<size_t> splitGen (
          static_cast<size_t> (qPos) + 1, qLen - 1
      );
      mSplit = splitGen (rng);
      delLen = delLenGen (rng);
    }

    std::string seq (qLen, ' ');
    std::string qual (qLen, ' ');
    for (size_t j = 0; j < qLen; ++j) {
      const size_t refOffset =
          static_cast<size_t> (ru_pf.start) + j +
          (j < mSplit ? 0 : delLen);
      const char refBase = refSeq[refOffset];
      seq[j] = mismatchDist (rng) ? mutate_base (refBase, rng)
                                  : refBase;
      qual[j] = static_cast<char> (qualGen (rng) + 33);
    }
    ru_pf.seqBases = std::move (seq);
    ru_pf.qualAscii = std::move (qual);

    if (delLen == 0) {
      ru_pf.rawCig = {static_cast<uint32_t> (
          bam_cigar_gen (qLen, BAM_CMATCH)
      )};
      ru_pf.nCig = 1;
      ru_pf.end = ru_pf.start + static_cast<hts_pos_t> (qLen);
    }
    else {
      ru_pf.rawCig = {
          static_cast<uint32_t> (
              bam_cigar_gen (mSplit, BAM_CMATCH)
          ),
          static_cast<uint32_t> (
              bam_cigar_gen (delLen, BAM_CDEL)
          ),
          static_cast<uint32_t> (
              bam_cigar_gen (qLen - mSplit, BAM_CMATCH)
          )
      };
      ru_pf.nCig = 3;
      ru_pf.end =
          ru_pf.start + static_cast<hts_pos_t> (qLen + delLen);
    }
    ru_pf.cig =
        stringify_cigar (ru_pf.rawCig.data(), ru_pf.nCig);

    ru_pf.qPos = qPos;
    ru_pf.base = ru_pf.seqBases[static_cast<size_t> (qPos)];
    ru_pf.baseQual = static_cast<uint8_t> (
        ru_pf.qualAscii[static_cast<size_t> (qPos)] - 33
    );
    ru_pf.isHead = (qPos == 0);
    ru_pf.isTail = (qPos == static_cast<int32_t> (qLen - 1));

    if (ru_pf.start < span.start) {
      span.start = ru_pf.start;
    }
    if (ru_pf.end > span.end) {
      span.end = ru_pf.end;
    }

    reads.push_back (std::move (ru_pf));
  }

  const auto refSlice = refSeq.substr (
      static_cast<size_t> (span.start),
      static_cast<size_t> (span.end - span.start)
  );

  // demo data has no real alignment file / contigs; placeholder
  // metadata row just satisfies the reads table's loci_id FK chain.
  AlnFile dummyAln;
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
    if (int sqlRc = bind_pileup_fields (stmt, lociId, ru_pf);
        sqlRc != SQLITE_OK) {
      Err err = make_sqlite3_err (sqlRc, sqlite3_errmsg (db));
      rollback_on_err (db, err);
      return std::unexpected{err};
    }

    if (int sqlRc = sqlite3_step (stmt); sqlRc != SQLITE_DONE) {
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
