#include "backend/pileup_ingest.hpp"

#include <htslib/hts.h>
#include <htslib/kstring.h>
#include <htslib/sam.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

#include "backend/hts_types.hpp"
#include "backend/sql.hpp"
#include "plog/Log.h"
#include "shared/err.hpp"

namespace {

std::expected<SqliteStmt, Err> prepare_insert_loci_stmt (
    PileupDB& db
)
{
  SqliteStmt stmt;
  int rc;
  if (rc = sqlite3_prepare_v2 (
          db, sh_sqlInsertLoci.data(),
          static_cast<int> (sh_sqlInsertLoci.size()),
          &stmt.o_stmt, NULL
      );
      rc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errmsg (db))
    };
  }
  return stmt;
}

}  // namespace

std::expected<SqliteStmt, Err> prepare_insert_reads_stmt (
    PileupDB& db
)
{
  SqliteStmt stmt;
  int rc;
  if (rc = sqlite3_prepare_v2 (
          db, sh_sqlInsertReads.data(),
          static_cast<int> (sh_sqlInsertReads.size()),
          &stmt.o_stmt, NULL
      );
      rc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errmsg (db))
    };
  }
  return stmt;
}

std::string stringify_cigar (const uint32_t* br_cig, size_t nCig)
{
  std::string out;
  for (size_t opi = 0; opi < nCig; opi++) {
    const auto cigel = br_cig[opi];
    out += std::to_string (bam_cigar_oplen (cigel));
    out += bam_cigar_opchr (cigel);
  }
  return out;
}

/* TAG CONVERSION */

// Escape a raw aux string value for embedding in a JSON string literal.
// SAM 'A'/'Z' values are drawn from [ !-~]+, which permits '"' and '\'
// unescaped — without this, valid tags can produce malformed JSON
// and trip the `reads.tags` CHECK(json_valid(tags)) constraint.
void append_json_escaped (
    const char* br_data, size_t len, std::string& out
)
{
  static const char hexDigits[] = "0123456789abcdef";
  for (size_t i = 0; i < len; ++i) {
    const auto ch = static_cast<unsigned char> (br_data[i]);
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
    const uint8_t* br_aux1, const uint8_t* br_auxEnd,
    std::string& entryOut
)
{
  // TODO: error strategy
  kstring_t o_kstr;
  ks_initialize (&o_kstr);
  if (sam_format_aux1 (
          br_aux1 - 2, *br_aux1, br_aux1 + 1, br_auxEnd, &o_kstr
      ) == NULL) {
    return std::unexpected{make_htslib_err (
        -1,
        "failed to parse aux "
        "tag"
    )};  // TODO: better error
  }
  const char* br_str = ks_str (&o_kstr);

  /* append key */
  entryOut += '"';  // open key quotes
  entryOut.append (br_str, 2);  // 2-ch tag
  entryOut += '"';  // close
  entryOut += ':';  // add key-val separator

  /* append val */
  const char typeCh = *(br_str + 3);
  if (typeCh == 'B') {
    entryOut += '[';  // open array
    // handle array
    // header is always "TAG:B:<subtype>" (2 + 1 + 2 + 1 = 6 chars);
    // htslib only emits the first ',' -- and therefore anything past
    // the header -- once there's at least one element, so an empty
    // array (ks_len == headerLen) must short-circuit here rather than
    // read past the end of the formatted buffer.
    const size_t headerLen = 6;
    if (ks_len (&o_kstr) > headerLen) {
      const char* br_valStart = br_str + headerLen + 1;
      // all allowed array types are numeric
      // no need to check type
      ks_tokaux_t tokAux;
      const char* br_tok;
      bool firstTok = true;
      for (br_tok = kstrtok (br_valStart, ",", &tokAux);
           br_tok != nullptr;
           br_tok = kstrtok (NULL, NULL, &tokAux)) {
        const size_t tokLen =
            static_cast<size_t> (tokAux.p - br_tok);
        if (!firstTok) {
          entryOut += ',';
        }
        entryOut.append (br_tok, tokLen);
        firstTok = false;
      }
    }
    entryOut += ']';
  }
  else {
    const size_t valStartOffset = 5;
    const char* br_valStart = br_str + valStartOffset;
    const size_t valLen = ks_len (&o_kstr) - valStartOffset;
    switch (typeCh) {
      case 'A':
      case 'Z':
      case 'H':
        // val as string
        entryOut += '"';
        append_json_escaped (br_valStart, valLen, entryOut);
        entryOut += '"';
        break;
      default:
        // val as numeric
        entryOut.append (br_valStart, valLen);
        break;
    }
  }

  ks_free (&o_kstr);
  return {};
}

extern "C" {
int pileup_func (void* br_data, bam1_t* br_b)
{
  const PileupCapture* br_d = (PileupCapture*)(br_data);
  // No filtering
  return sam_itr_next (br_d->br_fh, br_d->o_it, br_b);
}
}

PileupOrErr prepare_pileup (
    const AlnFile& aln, const PileupPosition& pos
)
{
  PLOGD << "Begin prepare_pileup";
  PreparedPileup out;

  PLOGD << "Initalising sam_itr_queryi";
  auto* o_alnIter =
      sam_itr_queryi (aln.o_idx, pos.tid, pos.pos, pos.pos + 1);
  if (o_alnIter == NULL) {
    return std::unexpected{make_htslib_err (
        -1,
        "sam_itr_queryi: failed "
        "to create iterator"
    )};
  }

  PLOGD << "Initialising bam_plp_t";
  out.o_cap = new PileupCapture{aln.o_fh, o_alnIter};
  auto* o_plp = bam_plp_init (pileup_func, out.o_cap);
  if (o_plp == NULL) {
    return std::unexpected{make_htslib_err (
        -1,
        "bam_plp_init: failed to initialise "
        "pileup engine"
    )};
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
      return std::unexpected{
          make_htslib_err (nPlp, "bam_plp64_auto: pileup failed")
      };
    }
    if (plpPos < pos.pos) {
      continue;  // doesn't cover variant
    }
    PLOGD << "Position found";
    out.br_plpArr = br_plpArr;
    out.nPlp = static_cast<size_t> (nPlp);
    return out;
  }
  PLOGD << "Position not covered by alignment file";
  return {};
}

GenomicSpan get_pileup_span (const PreparedPileup& plp)
{
  GenomicSpan out{INT64_MAX, 0};
  for (size_t i = 0; i < plp.nPlp; i++) {
    auto* const b1 = plp.br_plpArr[i].b;
    const auto rStart = b1->core.pos;
    const auto rEnd =
        rStart + bam_cigar2rlen (
                     static_cast<int> (b1->core.n_cigar),
                     bam_get_cigar (b1)
                 );
    out.start = std::min (out.start, rStart);
    out.end = std::max (out.end, rEnd);
  }
  return out;
}

[[nodiscard]] IntOrErr insert_loci (
    PileupDB& db, const LocusData& locus
)
{
  /*
    insert pileup loci into database, returning id.
    Uses automatic transaction handling.
  */
  auto r = prepare_insert_loci_stmt (db);
  if (!r) {
    return std::unexpected{r.error()};
  }
  auto stmt{std::move (*r)};

  int col = 1;
  int rc;
  if (rc = sqlite3_bind_text (
          stmt, col++, locus.contig.c_str(), -1, SQLITE_TRANSIENT
      );
      rc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errstr (rc))
    };
  }
  if (rc = sqlite3_bind_int64 (stmt, col++, locus.pos);
      rc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errstr (rc))
    };
  }
  if ((rc = sqlite3_bind_int64 (stmt, col++, locus.start)) !=
      SQLITE_OK) {
    return std::unexpected (
        make_sqlite3_err (rc, sqlite3_errstr (rc))
    );
  }
  if ((rc = sqlite3_bind_int64 (stmt, col++, locus.end)) !=
      SQLITE_OK) {
    return std::unexpected (
        make_sqlite3_err (rc, sqlite3_errstr (rc))
    );
  }
  if (!locus.refSlice) {
    if (rc = sqlite3_bind_null (stmt, col++); rc != SQLITE_OK) {
      return std::unexpected (
          make_sqlite3_err (rc, sqlite3_errstr (rc))
      );
    }
  }
  else {
    if (rc = sqlite3_bind_text (
            stmt, col++, (*locus.refSlice).c_str(),
            static_cast<int> ((*locus.refSlice).size()),
            SQLITE_TRANSIENT
        );
        rc != SQLITE_OK) {
      return std::unexpected (
          make_sqlite3_err (rc, sqlite3_errstr (rc))
      );
    }
  }

  if (rc = sqlite3_step (stmt); rc != SQLITE_DONE) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errmsg (db))
    };
  }
  return sqlite3_last_insert_rowid (db);
}

// Bind one pileup row's fields into `stmt`, in column order matching
// stmt_str_InsertReads. lociId identifies the locus this read belongs
// to. Returns the sqlite3 result code of the first failing bind call,
// or SQLITE_OK if all columns bound successfully.
[[nodiscard]] int bind_pileup_fields (
    SqliteStmt& stmt, sqlite3_int64 lociId,
    const PileupFields& pf
)
{
  // NOTE: returns sql error code directly;
  // outer scope needs to handle error/rollback anyway
  int col = 1;
  int sqlRc;
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
  if (sqlRc = sqlite3_bind_int (
          stmt, col++, static_cast<int> (pf.isDel)
      );
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int (
          stmt, col++, static_cast<int> (pf.isHead)
      );
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int (
          stmt, col++, static_cast<int> (pf.isTail)
      );
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int (
          stmt, col++, static_cast<int> (pf.isRefSkip)
      );
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
  if (sqlRc = sqlite3_bind_blob (
          stmt, col++, pf.rawCig.data(),
          static_cast<int> (pf.nCig << 2), SQLITE_TRANSIENT
      );
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  if (sqlRc = sqlite3_bind_int (
          stmt, col++, static_cast<int> (pf.nCig)
      );
      sqlRc != SQLITE_OK) {
    return sqlRc;
  }
  return SQLITE_OK;
}

VoidOrErr begin_transaction (PileupDB& db)
{
  if (const int sqlRc =
          sqlite3_exec (db, "BEGIN;", NULL, NULL, NULL);
      sqlRc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (sqlRc, sqlite3_errmsg (db))
    };
  }
  return {};
}

void rollback_on_err (PileupDB& db, Err& err)
{
  if (const int sqlRc =
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      sqlRc != SQLITE_OK) {
    err.msg += " (additionally, ROLLBACK failed with code " +
               std::to_string (sqlRc) +
               " and msg: " + sqlite3_errmsg (db) + ")";
  }
}

VoidOrErr commit (PileupDB& db)
{
  if (const int sqlRc =
          sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
      sqlRc != SQLITE_OK) {
    Err err = make_sqlite3_err (sqlRc, sqlite3_errmsg (db));
    rollback_on_err (db, err);
    return std::unexpected{err};
  }
  return {};
}

VoidOrErr insert_reads_internal (
    PileupDB& db, const bam_pileup1_t* br_plpArr, size_t nPlp,
    int lociId, const Tid2StrFn& tid2str
)
{
  if (nPlp == 0) {
    return {};
  }

  auto r = prepare_insert_reads_stmt (db);
  if (!r) {
    return std::unexpected{r.error()};  // no rollback needed
  }
  auto stmt{std::move (*r)};

  if (auto beginRet = begin_transaction (db); !beginRet) {
    return std::unexpected{
        beginRet.error()
    };  // BEGIN never succeeded, nothing to roll back
  }

  PLOGD << "Inserting reads";
  PileupFields ru_pf;
  bam_pileup1_t* ru_p1;
  bam1_t* ru_b1;
  const char* ru_mtidName = NULL;

  for (size_t i = 0; i < nPlp; ++i) {
    ru_p1 = const_cast<bam_pileup1_t*> (&br_plpArr[i]);
    {
      /* stringify mtid, if available */
      // '=' if same contig as this read, per SAM RNEXT convention;
      // NULL if no mate reference (core.mtid < 0).
      ru_b1 = ru_p1->b;
      if (ru_b1->core.mtid >= 0) {
        // NOTE: in the case where tid2name
        // fails, null recorded in database.
        // Hence failure not checked.
        ru_mtidName = (ru_b1->core.mtid == ru_b1->core.tid)
                          ? "="
                          : tid2str (ru_b1->core.mtid);
      }
      else {
        ru_mtidName = NULL;
      }
    }

    if (auto ffRet = fill_fields (ru_pf, ru_p1, ru_mtidName);
        !ffRet) {
      rollback_on_err (db, ffRet.error());
      return std::unexpected{ffRet.error()};
    }

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

// Convert a bam_pileup1_t
// into the pileup database
// interface type.
//
// NOTE: takes mTidName directly to
// avoid dealing with SAM header.
VoidOrErr fill_fields (
    PileupFields& pf, const bam_pileup1_t* br_p1,
    const char* mTidName
)
{
  const auto* br_b1 = br_p1->b;
  const auto nCig = br_b1->core.n_cigar;
  const auto* br_cig = bam_get_cigar (br_b1);

  pf.qPos = br_p1->qpos;
  pf.indel = br_p1->indel;
  pf.isDel = br_p1->is_del;
  pf.isHead = br_p1->is_head;
  pf.isTail = br_p1->is_tail;
  pf.isRefSkip = br_p1->is_refskip;
  // NOTE: qname null terminated,
  // so assignment safe.
  pf.qName = bam_get_qname (br_b1);
  pf.flag = br_b1->core.flag;
  pf.start = br_b1->core.pos;
  pf.mapQ = br_b1->core.qual;
  pf.mtidName = mTidName != NULL ? mTidName : "";
  pf.mStart = br_b1->core.mpos;  // <0 == unaligned (or no mate)
  pf.rawCig = {br_cig, br_cig + nCig};
  pf.nCig = br_b1->core.n_cigar;

  {
    /* stringify seq, qual */
    // ASSUMPTION: seq and qual data present.
    auto& pfSeq = pf.seqBases;
    auto& pfQual = pf.qualAscii;
    const auto lq = static_cast<size_t> (br_b1->core.l_qseq);
    pfSeq.resize (lq);
    pfQual.resize (lq);

    const uint8_t* br_qs = bam_get_seq (br_b1);
    const uint8_t* br_qq = bam_get_qual (br_b1);
    for (size_t j = 0; j < lq; ++j) {
      pfSeq[j] = seq_nt16_str[bam_seqi (br_qs, j)];
      pfQual[j] = static_cast<char> (br_qq[j] + 33);
    }
    pf.baseQual = br_qq[br_p1->qpos];
    pf.base = pfSeq[static_cast<size_t> (br_p1->qpos)];
  }

  {
    /* stringify cigar */
    // ASSUMPTION: cigar available and correct.
    pf.cig = stringify_cigar (br_cig, nCig);
    pf.end = pf.start +
             bam_cigar2rlen (static_cast<int> (nCig), br_cig);
  }

  {
    /* aux to json; left empty if no aux tags present */
    auto& auxJson = pf.auxJson;
    auxJson.clear();
    const uint8_t* br_dataEnd = br_b1->data + br_b1->l_data;
    const uint8_t* br_aux1 = bam_aux_first (br_b1);
    if (br_aux1 != NULL) {
      auxJson += '{';
      std::string ru_aux1{};
      for (; br_aux1 != nullptr;) {
        ru_aux1.clear();
        auto convRet =
            aux1_to_json (br_aux1, br_dataEnd, ru_aux1);
        if (!convRet) {
          return std::unexpected{convRet.error()};
        }
        auxJson += ru_aux1;
        br_aux1 = bam_aux_next (br_b1, br_aux1);
        if (br_aux1 == NULL) {
          break;
        }
        auxJson += ',';
      }
      auxJson += '}';
    }
  }

  return {};
}
