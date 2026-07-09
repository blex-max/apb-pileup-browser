#include "demo.hpp"

#include <cassert>
#include <cstdlib>
#include <random>
#include <string>

#include <htslib/sam.h>

#include "core/PileupDB.hpp"
#include "core/err.hpp"


static std::string random_base_seq (size_t len) {
  static const char bases[] = "ATCG";
  std::string out;
  for (size_t i = 0; i < len; ++i)
    out += bases[rand() % (sizeof(bases) - 1)];
  return out;
}

VoidOrErr insert_demo_data (PileupDB& i_db, size_t regWidth, size_t nQuery) {
  std::mt19937 rng;

  const hts_pos_t pileupPos = static_cast<hts_pos_t>((regWidth / 2) - 1);
  const auto qLen = static_cast<size_t>(pileupPos);
  const auto refSeq = random_base_seq(regWidth);

  std::uniform_int_distribution<size_t> gstartGen(0, qLen);

  InsertReadsStmt stmt;
  PileupFields ru_pf;

  int sqlRc = SQLITE_OK;
  if (sqlRc = prepare_insert_reads(i_db, stmt); sqlRc != SQLITE_OK) {
    goto err_db;
  };
  if (sqlRc = sqlite3_exec (i_db, "BEGIN;", NULL, NULL, NULL); sqlRc != SQLITE_OK) {
    goto err_db;
  }

  ru_pf.cig = std::to_string(qLen) + "M";  // always same
  ru_pf.qualAscii = std::string(qLen, 'F');
  ru_pf.baseQual = 'F' - 33;
  ru_pf.flag = 0;
  ru_pf.indel = 0;
  ru_pf.isDel = false;
  ru_pf.isRefSkip = false;
  ru_pf.mapQ = 30;
  ru_pf.mStart = -1;
  for (size_t i = 0; i < nQuery; ++i) {
    ru_pf.qName = "read" + std::to_string(i);
    ru_pf.start = gstartGen(rng);
    ru_pf.end = ru_pf.start + qLen;
    ru_pf.seqBases = refSeq.substr(static_cast<size_t>(ru_pf.start), qLen);
    ru_pf.qPos = pileupPos - ru_pf.start;
    ru_pf.base = ru_pf.seqBases[ru_pf.qPos];
    ru_pf.isHead = (ru_pf.qPos == 0);
    ru_pf.isTail = (ru_pf.qPos == (qLen - 1));

    if (sqlRc = bind_pileup_fields (stmt, ru_pf); sqlRc != SQLITE_OK) {
      if (const int rollbackRc = sqlite3_exec (i_db, "ROLLBACK;", NULL, NULL, NULL); rollbackRc != SQLITE_OK) {
        return std::unexpected{make_sqlite3_err (sqlRc,
            (std::string (sqlite3_errstr (sqlRc)) + " (additionally, ROLLBACK failed with code " + std::to_string(rollbackRc) + " and msg: " + sqlite3_errmsg(i_db) + ")").c_str())};
      }
      return std::unexpected{make_sqlite3_err (sqlRc, sqlite3_errstr (sqlRc))};
    }

    if (sqlRc = sqlite3_step (stmt); sqlRc != SQLITE_DONE) {
      goto err_db;
    }
    sqlite3_reset (stmt);           // rc mirrors the step already checked above
    sqlite3_clear_bindings (stmt);  // cannot fail per sqlite3 docs
  }

  if (sqlRc = sqlite3_exec (i_db, "COMMIT;", NULL, NULL, NULL); sqlRc != SQLITE_OK) {
    goto err_db;
  }

  return {};

err_db:
  {
    const std::string errMsg = sqlite3_errmsg (i_db);
    if (const int rollbackRc = sqlite3_exec (i_db, "ROLLBACK;", NULL, NULL, NULL); rollbackRc != SQLITE_OK) {
      return std::unexpected{make_sqlite3_err (sqlRc,
          (errMsg + " (additionally, ROLLBACK failed with code " + std::to_string(rollbackRc) + " and msg: " + sqlite3_errmsg(i_db) + ")").c_str())};
    }
    return std::unexpected{make_sqlite3_err (sqlRc, errMsg.c_str())};
  }

}

