#include "core/PileupDB.hpp"

#include <htslib/hts.h>
#include <htslib/kstring.h>
#include <htslib/sam.h>

#include <cstddef>
#include <cstring>
#include <expected>
#include <functional>
#include <string>

#include "core/PileupDB_internal.hpp"
#include "core/err.hpp"
#include "core/hts_types.hpp"
#include "core/sql.hpp"
#include "plog/Log.h"

DbOrErr make_db()
{
  PileupDB db;
  int sqlRc = 0;

  if (sqlRc = sqlite3_open (":memory:", &db.o_ptr);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  auto excFn = [&db] (const std::string_view stmt) -> int {
    return sqlite3_exec (db, stmt.data(), NULL, NULL, NULL);
  };

  if (sqlRc = excFn (rsql_SetTempStoreMemory);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  if (sqlRc = excFn (rsql_PragmaForeignKeys);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  if (sqlRc = excFn (rsql_CreateMetaDataTable);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  if (sqlRc = excFn (rsql_CreateLociTable); sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  if (sqlRc = excFn (rsql_CreateLociSampleIdIndex);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  if (sqlRc = excFn (rsql_CreateReadsTable);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  if (sqlRc = excFn (rsql_CreateReadsLociIdIndex);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  if (sqlRc = excFn (rsql_CreateReadsSampleIdIndex);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }

  return db;
}

VoidOrErr dump_to_disk (
    const PileupDB& db, const std::string& path
)
{
  /*
    Copy the in-memory database out to a file on disk,
    via sqlite3's online backup API.
  */
  int sqlRc = SQLITE_OK;
  sqlite3* o_fileDb = NULL;
  sqlite3_backup* o_backup = NULL;

  if (sqlRc = sqlite3_open (path.c_str(), &o_fileDb);
      sqlRc != SQLITE_OK) {
    goto err_sql;
  }

  if (o_backup =
          sqlite3_backup_init (o_fileDb, "main", db, "main");
      o_backup == NULL) {
    sqlRc = sqlite3_errcode (o_fileDb);
    goto err_sql;
  }

  // -1: copy all remaining pages in a single step.
  // ASSUMPTION: db is quiescent (no concurrent writer holding
  // a lock) for the duration of the copy.
  if (sqlRc = sqlite3_backup_step (o_backup, -1);
      sqlRc != SQLITE_DONE) {
    sqlite3_backup_finish (o_backup);
    goto err_sql;
  }

  if (sqlRc = sqlite3_backup_finish (o_backup);
      sqlRc != SQLITE_OK) {
    goto err_sql;
  }
  o_backup = NULL;  // fin

  if (sqlRc = sqlite3_close_v2 (o_fileDb); sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errstr (sqlRc))
    };
  }
  o_fileDb = NULL;

  return {};

err_sql: {
  // NOTE: per sqlite3 docs, errors from backup_init/backup_step
  // are stored on the *destination* handle, so o_fileDb is the
  // right handle to query here in every failure case above.
  const std::string errMsg = sqlite3_errmsg (o_fileDb);
  sqlite3_close_v2 (o_fileDb);
  return std::unexpected{make_sqlite3_err (sqlRc, errMsg)};
}
}

/* pileup details */

/* TAG CONVERSION */

// Escape a raw aux string value for embedding in a JSON string literal.
// SAM 'A'/'Z' values are drawn from [ !-~]+, which permits '"' and '\'
// unescaped — without this, valid tags can produce malformed JSON
// and trip the `reads.tags` CHECK(json_valid(tags)) constraint.
void append_json_escaped (
    const char* p_data, size_t len, std::string& out
)
{
  static const char hexDigits[] = "0123456789abcdef";
  for (size_t i = 0; i < len; ++i) {
    const unsigned char ch =
        static_cast<unsigned char> (p_data[i]);
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      default:
        if (ch < 0x20) {
          out += "\\u00";
          out += hexDigits[(ch >> 4) & 0xF];
          out += hexDigits[ch & 0xF];
        }
        else {
          out += static_cast<char> (ch);
        }
    }
  }
}

VoidOrErr aux1_to_json (
    const uint8_t* p_aux1, const uint8_t* p_auxEnd,
    std::string& entryOut
)
{
  // TODO: error strategy
  kstring_t o_kstr;
  ks_initialize (&o_kstr);
  if (sam_format_aux1 (
          p_aux1 - 2, *p_aux1, p_aux1 + 1, p_auxEnd, &o_kstr
      ) == NULL) {
    return std::unexpected{make_htslib_err (
        -1,
        "failed to parse aux "
        "tag"
    )};  // TODO: better error
  }
  const char* p_str = ks_str (&o_kstr);

  /* append key */
  entryOut += '"';  // open key quotes
  entryOut.append (p_str, 2);  // 2-ch tag
  entryOut += '"';  // close
  entryOut += ':';  // add key-val separator

  /* append val */
  const char typeCh = *(p_str + 3);
  if (typeCh == 'B') {
    entryOut += '[';  // open array
    // handle array
    const size_t valStartOffset = 7;
    const char* p_valStart = p_str + valStartOffset;
    // all allowed array types are numeric
    // no need to check type
    ks_tokaux_t tokAux;
    const char* p_tok;
    for (p_tok = kstrtok (p_valStart, ",", &tokAux); p_tok;
         p_tok = kstrtok (NULL, NULL, &tokAux)) {
      const size_t tokLen =
          static_cast<size_t> (tokAux.p - p_tok);
      entryOut.append (p_tok, tokLen);
      entryOut += ',';
    }
    entryOut += ']';
  }
  else {
    const size_t valStartOffset = 5;
    const char* p_valStart = p_str + valStartOffset;
    const size_t valLen = ks_len (&o_kstr) - valStartOffset;
    switch (typeCh) {
      case 'A':
      case 'Z':
      case 'H':
        // val as string
        entryOut += '"';
        append_json_escaped (p_valStart, valLen, entryOut);
        entryOut += '"';
        break;
      default:
        // val as numeric
        entryOut.append (p_valStart, valLen);
        break;
    }
  }

  ks_free (&o_kstr);
  return {};
}

extern "C" {
int pileup_func (void* data, bam1_t* b)
{
  PileupCapture* d = (PileupCapture*)(data);
  // No filtering
  return sam_itr_next (d->uo_fh, d->o_it, b);
}
}

PileupOrErr prepare_pileup (
    const AlnFile& aln, const PileupPosition& pos
)
{
  PLOGD << "Begin prepare_pileup";
  PreparedPileup out;

  PLOGD << "Initalising sam_itr_queryi";
  auto alnIter =
      sam_itr_queryi (aln.o_idx, pos.tid, pos.pos, pos.pos + 1);
  if (alnIter == NULL) {
    return std::unexpected{make_htslib_err (
        -1,
        "sam_itr_queryi: failed "
        "to create iterator"
    )};
  }

  PLOGD << "Initialising bam_plp_t";
  out.o_cap = new PileupCapture{aln.o_fh, alnIter};
  auto plp = bam_plp_init (pileup_func, out.o_cap);
  if (plp == NULL) {
    return std::unexpected{make_htslib_err (
        -1,
        "bam_plp_init: failed to initialise "
        "pileup engine"
    )};
  }
  out.o_plp = plp;

  int64_t plpPos = -1;
  int plpTid = -1;
  int nPlp = -1;
  const bam_pileup1_t* plpArr;
  PLOGD << "Iterating pileup";
  while ((plpArr = bam_plp64_auto (
              out.o_plp, &plpTid, &plpPos, &nPlp
          )) != 0) {
    if (nPlp < 0 || plpTid < 0 || plpPos < 0) {
      return std::unexpected{
          make_htslib_err (nPlp, "bam_plp64_auto: pileup failed")
      };
    }
    if (plpPos < pos.pos) {
      continue;  // doesn't cover variant
    }
    PLOGD << "Position found";
    out.plpArr = const_cast<const bam_pileup1_t*> (plpArr);
    out.nPlp = static_cast<size_t> (nPlp);
    return std::move (out);
  }
  PLOGD << "Position not covered by alignment file";
  return {};
}

using IntOrErr = std::expected<int, Err>;
IntOrErr insert_sample (PileupDB& db, const AlnFile& _)
{
  /*
    insert sample metadata into database, returning id.

    uses automatic transaction handling.
  */
  // NOTE: automatic transaction handling
  // means no need to handle rollback on
  // error paths.
  auto r = prepare<InsertSampleStmt> (db);
  if (!r) {
    return std::unexpected{r.error()};
  }
  auto stmt{std::move (*r)};

  int col = 1;
  int rc;
  // placeholder bind;
  // in future will use data from AlnFile
  if (rc = sqlite3_bind_int (stmt, col, 1); rc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errstr (rc))
    };
  }
  if (rc = sqlite3_step (stmt); rc != SQLITE_DONE) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errmsg (db))
    };
  }
  return sqlite3_last_insert_rowid (db);
}

[[nodiscard]] IntOrErr insert_loci (
    PileupDB& db, const PileupPosition& pos, int alnId,
    Tid2StrFn tid2str
)
{
  /*
    insert pileup loci into database, returning id.

    uses automatic transaction handling.
  */
  auto r = prepare<InsertLociStmt> (db);
  if (!r) {
    return std::unexpected{r.error()};
  }
  auto stmt{std::move (*r)};

  // NOTE: handling the text of tid names
  // a lot, in different places. Might
  // be easier to store a contigs table
  // and just use an integer key in
  // other tables.
  const char* contigName = tid2str (pos.tid);
  if (contigName == NULL) {
    return std::unexpected{make_htslib_err (
        -1,
        "sam_hdr_tid2name: failed to resolve "
        "contig name for tid " +
            std::to_string (pos.tid)
    )};
  }

  int col = 1;
  int rc;
  if (rc = sqlite3_bind_int64 (stmt, col++, alnId);
      rc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errstr (rc))
    };
  }
  if (rc = sqlite3_bind_text (
          stmt, col++, contigName, -1, SQLITE_TRANSIENT
      );
      rc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errstr (rc))
    };
  }
  if (rc = sqlite3_bind_int64 (stmt, col++, pos.pos);
      rc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errstr (rc))
    };
  }

  if (rc = sqlite3_step (stmt); rc != SQLITE_DONE) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errmsg (db))
    };
  }
  return sqlite3_last_insert_rowid (db);
}

VoidOrErr insert_pileup (
    PileupDB& db, const AlnFile& aln, const PileupPosition& pos,
    int alnId
)
{
  auto ppRet = prepare_pileup (aln, pos);
  if (!ppRet) {
    return std::unexpected{ppRet.error()};
  }
  auto reads{std::move (*ppRet)};

  // NOTE: not checking if loci already exists
  // NOTE: not recording nreads in loci table;
  // it would be good to do so as indication
  // of whether to expect reads in reads table
  // for this loci
  auto tid2str = [&aln] (int tid) {
    return sam_hdr_tid2name (aln.o_hdr, tid);
  };
  auto ilRet = insert_loci (db, pos, alnId, tid2str);
  if (!ilRet) {
    return std::unexpected{ilRet.error()};
  }
  auto lociId = *ilRet;

  auto ipiRet = insert_reads_internal (
      db, reads.plpArr, reads.nPlp, alnId, lociId, tid2str
  );
  if (!ipiRet) {
    return std::unexpected{ipiRet.error()};
  }
  return {};
};

// Bind one pileup row's fields into `stmt`, in column order matching
// stmt_str_InsertReads. sampleId/lociId identify the tranche (sample, locus)
// this read belongs to. Returns the sqlite3 result code of the first
// failing bind call, or SQLITE_OK if all columns bound successfully.
[[nodiscard]] int bind_pileup_fields (
    InsertReadsStmt& stmt, sqlite3_int64 sampleId,
    sqlite3_int64 lociId, const PileupFields& pf
)
{
  // NOTE: returns sql error code directly;
  // outer scope needs to handle error/rollback anyway
  int col = 1;
  int sqlRc;
  if (sqlRc = sqlite3_bind_int64 (stmt, col++, sampleId);
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int64 (stmt, col++, lociId);
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_text (
          stmt, col++, pf.qName.data(),
          static_cast<int> (pf.qName.size()), SQLITE_TRANSIENT
      );
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int (stmt, col++, pf.flag);
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int64 (stmt, col++, pf.start);
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int64 (stmt, col++, pf.end);
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int (stmt, col++, pf.mapQ);
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_text (
          stmt, col++, &pf.base, 1, SQLITE_TRANSIENT
      );
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int (stmt, col++, pf.baseQual);
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int64 (stmt, col++, pf.qPos);
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int (stmt, col++, pf.indel);
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int (stmt, col++, pf.isDel);
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int (stmt, col++, pf.isHead);
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int (stmt, col++, pf.isTail);
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int (stmt, col++, pf.isRefSkip);
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_text (
          stmt, col++, pf.cig.data(),
          static_cast<int> (pf.cig.size()), SQLITE_TRANSIENT
      );
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_text (
          stmt, col++, pf.seqBases.data(),
          static_cast<int> (pf.seqBases.size()), SQLITE_TRANSIENT
      );
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_text (
          stmt, col++, pf.qualAscii.data(),
          static_cast<int> (pf.qualAscii.size()),
          SQLITE_TRANSIENT
      );
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (!pf.mtidName.empty()) {
    if (sqlRc = sqlite3_bind_text (
            stmt, col++, pf.mtidName.data(),
            static_cast<int> (pf.mtidName.size()),
            SQLITE_TRANSIENT
        );
        sqlRc != SQLITE_OK) {
      return sqlRc;
    }
  }
  else {
    if (sqlRc = sqlite3_bind_null (stmt, col++);
        sqlRc != SQLITE_OK) {
      return sqlRc;
    }
  }
  if (pf.mStart < 0) {
    if (sqlRc = sqlite3_bind_null (stmt, col++);
        sqlRc != SQLITE_OK) {
      return sqlRc;
    }
  }
  else {
    if (sqlRc = sqlite3_bind_int64 (stmt, col++, pf.mStart);
        sqlRc != SQLITE_OK) {
      return sqlRc;
    }
  }
  if (pf.auxJson.empty()) {
    if (sqlRc = sqlite3_bind_null (stmt, col++);
        sqlRc != SQLITE_OK) {
      return sqlRc;
    }
  }
  else {
    if (sqlRc = sqlite3_bind_text (
            stmt, col++, pf.auxJson.data(),
            static_cast<int> (pf.auxJson.size()),
            SQLITE_TRANSIENT
        );
        sqlRc != SQLITE_OK) {
      return sqlRc;
    }
  }
  return SQLITE_OK;
}

VoidOrErr insert_reads_internal (
    PileupDB& db, const bam_pileup1_t* plpArr, size_t nPlp,
    int alnId, int lociId, Tid2StrFn tid2str
)
{
  if (nPlp == 0) {
    return {};
  }
  int sqlRc;
  // loop buffers
  PileupFields ru_pf;
  bam_pileup1_t* ru_p1;
  bam1_t* ru_b1;
  char* ru_mtidName = NULL;

  auto r = prepare<InsertReadsStmt> (db);
  if (!r) {
    return std::unexpected{r.error()};  // no rollback needed
  }
  auto stmt{std::move (*r)};

  if (sqlRc = sqlite3_exec (db, "BEGIN;", NULL, NULL, NULL);
      sqlRc != SQLITE_OK) {
    goto err_db;
  }

  PLOGD << "Inserting reads";
  for (size_t i = 0; i < nPlp; ++i) {
    ru_p1 = const_cast<bam_pileup1_t*> (&plpArr[i]);
    {
      /* stringify mtid, if available */
      // '=' if same contig as this read, per SAM RNEXT convention;
      // NULL if no mate reference (core.mtid < 0).
      ru_b1 = ru_p1->b;
      if (ru_b1->core.mtid >= 0) {
        // NOTE: in the case where tid2name
        // fails, null recorded in database.
        // Hence failure not checked.
        ru_mtidName = const_cast<char*> (
            (ru_b1->core.mtid == ru_b1->core.tid)
                ? "="
                : tid2str (ru_b1->core.mtid)
        );
      }
      else {
        ru_mtidName = NULL;
      }
    }

    if (auto ffRet = fill_fields (ru_pf, ru_p1, ru_mtidName);
        !ffRet) {
      if (const int rollbackRc =
              sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          rollbackRc != SQLITE_OK) {
        ffRet.error().msg +=
            "(additionally, ROLLBACK failed with "
            "code " +
            std::to_string (rollbackRc) +
            " and msg: " + sqlite3_errmsg (db) + ")";
      }
      else {
        // NOTE rollback success not indicated on other failure paths
        ffRet.error().msg +=
            "(database transaction has been "
            "rolled back)";
      }
      return std::unexpected{ffRet.error()};
    }

    if (sqlRc = bind_pileup_fields (stmt, alnId, lociId, ru_pf);
        sqlRc != SQLITE_OK) {
      goto err_rc;
    }

    if (sqlRc = sqlite3_step (stmt); sqlRc != SQLITE_DONE) {
      goto err_db;
    }
    sqlite3_reset (
        stmt
    );  // rc mirrors the step already checked above
    sqlite3_clear_bindings (
        stmt
    );  // cannot fail per sqlite3 docs
  }

  if (sqlRc = sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
      sqlRc != SQLITE_OK) {
    goto err_db;
  }

  return {};

err_db: {
  const std::string errMsg = sqlite3_errmsg (db);
  // could factor out rollback
  if (const int rollbackRc =
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      rollbackRc != SQLITE_OK) {
    return std::unexpected{make_sqlite3_err (
        sqlRc, (errMsg +
                " (additionally, ROLLBACK failed "
                "with code " +
                std::to_string (rollbackRc) +
                " and msg: " + sqlite3_errmsg (db) + ")")
    )};
  }
  return std::unexpected{make_sqlite3_err (sqlRc, errMsg)};
}

err_rc: {
  if (const int rollbackRc =
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      rollbackRc != SQLITE_OK) {
    return std::unexpected{make_sqlite3_err (
        sqlRc, (std::string (sqlite3_errstr (sqlRc)) +
                " (additionally, ROLLBACK failed with "
                "code " +
                std::to_string (rollbackRc) +
                " and msg: " + sqlite3_errmsg (db) + ")")
    )};
  }
  return std::unexpected{
      make_sqlite3_err (sqlRc, sqlite3_errstr (sqlRc))
  };
}
}

// Convert a bam_pileup1_t
// into the pileup database
// interface type.
//
// NOTE: takes mTidName directly to
// avoid dealing with SAM header.
VoidOrErr fill_fields (
    PileupFields& pf, const bam_pileup1_t* p1,
    const char* mTidName
)
{
  const auto uo_b1 = p1->b;

  pf.qPos = p1->qpos;
  pf.indel = p1->indel;
  pf.isDel = p1->is_del;
  pf.isHead = p1->is_head;
  pf.isTail = p1->is_tail;
  pf.isRefSkip = p1->is_refskip;
  // NOTE: qname null terminated,
  // so assignment safe.
  pf.qName = bam_get_qname (uo_b1);
  pf.flag = uo_b1->core.flag;
  pf.start = uo_b1->core.pos;
  pf.mapQ = uo_b1->core.qual;
  pf.mtidName = mTidName != NULL ? mTidName : "";
  pf.mStart = uo_b1->core.mpos;  // <0 == unaligned (or no mate)

  {
    /* stringify seq, qual */
    // ASSUMPTION: seq and qual data present.
    auto& ru_seq = pf.seqBases;
    auto& ru_qual = pf.qualAscii;
    const auto lq = static_cast<size_t> (uo_b1->core.l_qseq);
    ru_seq.resize (lq);
    ru_qual.resize (lq);

    const uint8_t* p_qs = bam_get_seq (uo_b1);
    const uint8_t* p_qq = bam_get_qual (uo_b1);
    for (size_t j = 0; j < lq; ++j) {
      ru_seq[j] = seq_nt16_str[bam_seqi (p_qs, j)];
      ru_qual[j] = static_cast<char> (p_qq[j] + 33);
    }
    pf.baseQual = p_qq[p1->qpos];
    pf.base = ru_seq[static_cast<size_t> (p1->qpos)];
  }

  {
    /* stringify cigar */
    // ASSUMPTION: cigar available and correct.
    auto& cig = pf.cig;
    cig.clear();  // final len unknown, can't resize directly.

    const auto nop = uo_b1->core.n_cigar;
    const auto p_cig = bam_get_cigar (uo_b1);
    // stringify cigar
    for (size_t opi = 0; opi < nop; opi++) {
      const auto cigel = p_cig[opi];
      const auto len = bam_cigar_oplen (cigel);
      const auto opc = bam_cigar_opchr (cigel);
      cig += std::to_string (len);
      cig += opc;
    }
    pf.end = pf.start +
             bam_cigar2rlen (static_cast<int> (nop), p_cig);
  }

  {
    /* aux to json; left empty if no aux tags present */
    auto& auxJson = pf.auxJson;
    auxJson.clear();
    const uint8_t* p_dataEnd = uo_b1->data + uo_b1->l_data;
    const uint8_t* p_aux1 = bam_aux_first (uo_b1);
    if (p_aux1 != NULL) {
      auxJson += '{';
      std::string ru_aux1{};
      for (; p_aux1;) {
        ru_aux1.clear();
        auto convRet = aux1_to_json (p_aux1, p_dataEnd, ru_aux1);
        if (!convRet) {
          return std::unexpected{convRet.error()};
        }
        auxJson += ru_aux1;
        p_aux1 = bam_aux_next (uo_b1, p_aux1);
        if (p_aux1 == NULL) {
          break;
        }
        auxJson += ',';
      }
      auxJson += '}';
    }
  }

  return {};
}
