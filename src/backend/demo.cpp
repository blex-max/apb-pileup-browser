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
    out += bases[rand() % (sizeof (bases) - 1)];
  }
  return out;
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

  std::uniform_int_distribution<size_t> gstartGen (0, qLen);

  // demo data has no real alignment file / contigs; placeholder
  // metadata + loci rows just satisfy the reads table's loci_id FK.
  AlnFile dummyAln;
  auto imRet = insert_metadata (db, dummyAln);
  if (!imRet) {
    return std::unexpected{imRet.error()};
  }

  auto ilRet =
      insert_loci (db, LocusData{"demo", 0, 0, 0, std::nullopt});
  if (!ilRet) {
    return std::unexpected{ilRet.error()};
  }
  const int lociId = *ilRet;

  auto stmtRet = prepare_insert_reads_stmt (db);
  if (!stmtRet) {
    return std::unexpected{stmtRet.error()};
  }
  auto stmt{std::move (*stmtRet)};
  PileupFields ru_pf;

  if (auto beginRet = begin_transaction (db); !beginRet) {
    return std::unexpected{beginRet.error()};
  }

  ru_pf.rawCig = {
      static_cast<uint32_t> (bam_cigar_gen (qLen, BAM_CMATCH))
  };
  ru_pf.nCig = 1;
  ru_pf.cig = std::to_string (qLen) + "M";  // always same
  ru_pf.qualAscii = std::string (qLen, 'F');
  ru_pf.baseQual = 'F' - 33;
  ru_pf.flag = 0;
  ru_pf.indel = 0;
  ru_pf.isDel = false;
  ru_pf.isRefSkip = false;
  ru_pf.mapQ = 30;
  ru_pf.mStart = -1;
  for (size_t i = 0; i < nQuery; ++i) {
    ru_pf.qName = "read" + std::to_string (i);
    ru_pf.start = gstartGen (rng);
    ru_pf.end = ru_pf.start + qLen;
    ru_pf.seqBases =
        refSeq.substr (static_cast<size_t> (ru_pf.start), qLen);
    ru_pf.qPos = pileupPos - ru_pf.start;
    ru_pf.base = ru_pf.seqBases[ru_pf.qPos];
    ru_pf.isHead = (ru_pf.qPos == 0);
    ru_pf.isTail = (ru_pf.qPos == (qLen - 1));

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
