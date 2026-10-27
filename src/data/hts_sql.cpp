#include "hts_sql.hpp"

#include <fmt/format.h>
#include <htslib/hts.h>
#include <htslib/kstring.h>
#include <htslib/sam.h>
#include <sqlite3.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "hts_types.hpp"
#include "schema.hpp"
#include "shared/apb_assert.hpp"
#include "shared/cleanup.hpp"

#define TRY_OK_INT(expr)                         \
  do {                                           \
    if (const auto rc = expr; rc != SQLITE_OK) { \
      return rc;                                 \
    }                                            \
  } while (false)

#define TRY_OK_EXP(expr)                         \
  do {                                           \
    if (const auto rc = expr; rc != SQLITE_OK) { \
      return std::unexpected (rc);               \
    }                                            \
  } while (false)

static int exec_sql (sqlite3* conn, const std::string_view sql)
{
  return sqlite3_exec (conn, std::string{sql}.c_str(), NULL, NULL, NULL);
};

int apb_init_db (sqlite3* conn)
{
  TRY_OK_INT (sqlite3_open (":memory:", &conn));

  TRY_OK_INT (exec_sql (conn, schema::sqlSetAppId));
  TRY_OK_INT (exec_sql (conn, schema::sqlSetVersion));
  TRY_OK_INT (exec_sql (conn, schema::sqlSetTempStoreMemory));
  TRY_OK_INT (exec_sql (conn, schema::sqlSetFK));
  TRY_OK_INT (exec_sql (conn, schema::sqlCreateLocusTable));
  TRY_OK_INT (exec_sql (conn, schema::sqlCreateFileTable));
  TRY_OK_INT (exec_sql (conn, schema::sqlCreatePerRecordTable));
  TRY_OK_INT (exec_sql (conn, schema::sqlCreateUserView));

  return SQLITE_OK;
}

static int read_int_pragma (sqlite3* conn, const char* sql, int& result)
{
  sqlite3_stmt* o_stmt = NULL;
  TRY_OK_INT (sqlite3_prepare_v2 (conn, sql, -1, &o_stmt, NULL));
  Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

  int rc = sqlite3_step (o_stmt);
  if (rc != SQLITE_ROW) {
    return rc;
  }

  result = sqlite3_column_int (o_stmt, 0);
  return SQLITE_OK;
}

static int schema_fingerprint (sqlite3* conn, std::string& result)
{
  sqlite3_stmt* o_stmt = NULL;
  if (const auto rc = sqlite3_prepare_v2 (
          conn,
          "SELECT m.name, p.name, p.type "
          "FROM sqlite_master m, pragma_table_info(m.name) p "
          "WHERE m.type = 'table' AND m.name NOT LIKE 'sqlite_%' "
          "ORDER BY m.name, p.cid;",
          -1, &o_stmt, NULL
      );
      rc != SQLITE_OK) {
        // FIXME: abort
  }
  Defer stmt_cleanup ([&]() { sqlite3_finalize (o_stmt); });

  std::string fingerprint;
  int rc;
  while ((rc = sqlite3_step (o_stmt)) == SQLITE_ROW) {
    for (int i = 0; i < 3; ++i) {
      const char* text =
          reinterpret_cast<const char*> (sqlite3_column_text (o_stmt, i));
      fingerprint += (text == nullptr) ? "" : text;
      fingerprint += (i < 2) ? ':' : '\n';
    }
  }
  if (rc != SQLITE_DONE) {
    return rc;
  }
  result = fingerprint;
  return SQLITE_OK;
}

std::expected<apb_verify_result_t, int> apb_verify_schema (sqlite3* conn)
{
  int pragmaResult;
  if (const auto rc =
          read_int_pragma (conn, "PRAGMA application_id;", pragmaResult);
      rc != SQLITE_OK) {
    return std::unexpected (rc);
  }
  if (pragmaResult != APB_APP_ID) {
    return apb_verify_result_t::appMismatch;
  }
  if (const auto rc =
          read_int_pragma (conn, "PRAGMA user_version;", pragmaResult);
      rc != SQLITE_OK) {
    return std::unexpected (rc);
  }
  if (pragmaResult != APB_SCHEMA_VER) {
    return apb_verify_result_t::verMismatch;
  }
  // Verify the loaded schema matches apb's expected schema.
  sqlite3* refdb = nullptr;
  if (const auto rc = apb_init_db (refdb); rc != SQLITE_OK) {
    return std::unexpected (rc);
  }
  Defer refdbCleanup ([&]() { sqlite3_close_v2 (refdb); });

  std::string refPrint;
  if (const auto rc = schema_fingerprint (refdb, refPrint);
      rc != SQLITE_OK) {
    return std::unexpected (rc);
  }
  std::string testPrint;
  if (const auto rc = schema_fingerprint (conn, testPrint);
      rc != SQLITE_OK) {
    return std::unexpected (rc);
  }
  if (refPrint != testPrint) {
    return apb_verify_result_t::schemaMismatch;
  }

  return apb_verify_result_t::success;
}

/* -- internal helpers -- */

/* TAG CONVERSION */

// Escape a raw aux string value for embedding in a JSON string literal.
// SAM 'A'/'Z' values are drawn from [ !-~]+, which permits '"' and '\'
// unescaped. Thererfore without this, valid tags can produce malformed JSON
// and trip the `reads.tags` CHECK(json_valid(tags)) constraint.
static void append_json_escaped (
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

struct Aux1ToJsonErr {
  enum Codes : uint8_t {
    parseFail,
  };
};
// converts single aux tag to json entry
// returns unexpected on failure to parse aux tag.
static std::expected<std::string, Aux1ToJsonErr::Codes> aux1_to_json (
    const uint8_t* aux1Start, const uint8_t* aux1End
)
{
  kstring_t o_kstr;
  ks_initialize (&o_kstr);
  Defer kstrCleanup ([&]() { ks_free (&o_kstr); });
  if (sam_format_aux1 (
          aux1Start - 2, *aux1Start, aux1Start + 1, aux1End, &o_kstr
      ) == NULL) {
    return std::unexpected (Aux1ToJsonErr::Codes::parseFail);
  }
  const char* br_str = ks_str (&o_kstr);

  /* append key */
  std::string out{'"'};  // open key quotes
  out.append (br_str, 2);  // 2-ch tag
  out += '"';  // close
  out += ':';  // add key-val separator

  /* append val */
  const char typeCh = *(br_str + 3);
  if (typeCh == 'B') {
    // array aux tag
    out += '[';  // open JSON array
    // form "TAG:B:<subtype>" (2 + 1 + 2 + 1 = 6 chars);
    constexpr auto arrayTagPrefixLen = 6;
    if (ks_len (&o_kstr) > arrayTagPrefixLen) {
      const char* payloadStartPtr = br_str + arrayTagPrefixLen + 1;
      // all allowed array types are numeric
      // no need to check type
      ks_tokaux_t tokAux;
      const char* tok;
      bool firstTok = true;
      for (tok = kstrtok (payloadStartPtr, ",", &tokAux); tok != nullptr;
           tok = kstrtok (NULL, NULL, &tokAux)) {
        const size_t tokLen = static_cast<size_t> (tokAux.p - tok);
        if (!firstTok) {
          out += ',';
        }
        // JSON has no nan/inf (finite values end in a digit)
        if (tokLen > 0 &&
            (isdigit (static_cast<unsigned char> (tok[tokLen - 1])) !=
             0)) {
          out.append (tok, tokLen);
        }
        else {
          out += "null";
        }
        firstTok = false;
      }
    }
    out += ']';  // close JSON array
  }
  else {
    constexpr auto tagPrefixLen = 5;
    const auto* payloadStartPtr = br_str + tagPrefixLen;
    const size_t payloadLen = ks_len (&o_kstr) - tagPrefixLen;
    switch (typeCh) {
      case 'A':
      case 'Z':
      case 'H':
        // payload as string
        out += '"';
        append_json_escaped (payloadStartPtr, payloadLen, out);
        out += '"';
        break;
      default:
        // payload as numeric; JSON has no nan/inf (finite values
        // end in a digit)
        if (payloadLen > 0 && (isdigit (
                                   static_cast<unsigned char> (
                                       payloadStartPtr[payloadLen - 1]
                                   )
                               ) != 0)) {
          out.append (payloadStartPtr, payloadLen);
        }
        else {
          out += "null";
        }
        break;
    }
  }

  return out;
}

// flat layout of htslib data to be entered
// into the database for a single read.
struct apb_pileup_t {
  // 0-INDEXED: MATCHES RAW HTSLIB REPRESENTATION.

  // NOTE: layout as table schema
  std::string qName = "?";
  uint16_t flag = UINT16_MAX;
  hts_pos_t start = -1;
  hts_pos_t end = -1;
  uint8_t mapQ = UINT8_MAX;

  std::optional<char> base = std::nullopt;
  std::optional<uint8_t> baseQual = std::nullopt;
  int32_t qPos = -1;
  int indel = INT_MAX;
  bool isDel = false, isHead = false, isTail = false, isRefSkip = false;

  std::string cig;
  std::optional<std::string> seq = std::nullopt;
  std::optional<std::vector<uint8_t>> rawQual = std::nullopt;

  std::optional<std::string> mtidName = std::nullopt;
  std::optional<hts_pos_t> mStart = std::nullopt;

  std::string auxJson;

  std::vector<uint32_t> rawCig;
  size_t nCig = SIZE_MAX;

  bool valid() const noexcept
  {
    return start >= 0 && end > start && qPos >= 0 &&
           (!seq || !rawQual || seq->size() == rawQual->size()) &&
           nCig == rawCig.size() && !rawCig.empty();
  }

  std::string to_string() const
  {
    return fmt::format (
        "PileupFields{{start: {}, end: {}, qPos: {}, "
        "nCig: {}, rawCig.size: {}}}",
        start, end, qPos, nCig, rawCig.size()
    );
  }
};

// Convert a bam_pileup1_t
// into the pileup database
// interface type.
//
// NOTE: takes mTidName directly to
// avoid dealing with SAM header.
static bool fill_fields (
    apb_pileup_t& pf, const bam_pileup1_t* br_p1, const char* mTidName
)
{
  const auto* br_b1 = br_p1->b;
  const auto nCig = br_b1->core.n_cigar;
  const auto* br_cig = bam_get_cigar (br_b1);

  pf.qPos = br_p1->qpos;
  pf.start = br_b1->core.pos;
  if (br_b1->core.mpos >= 0) {  // <0 == unaligned (or no mate)
    pf.mStart = br_b1->core.mpos;
  }

  pf.indel = br_p1->indel;
  pf.isDel = br_p1->is_del;
  pf.isHead = br_p1->is_head;
  pf.isTail = br_p1->is_tail;
  pf.isRefSkip = br_p1->is_refskip;
  // NOTE: qname null terminated,
  // so assignment safe.
  pf.qName = bam_get_qname (br_b1);
  pf.flag = br_b1->core.flag;
  pf.mapQ = br_b1->core.qual;
  if (mTidName != NULL) {
    pf.mtidName = mTidName;
  }
  pf.rawCig = {br_cig, br_cig + nCig};
  pf.nCig = br_b1->core.n_cigar;

  {
    /* stringify seq, qual */
    const auto lq = static_cast<size_t> (br_b1->core.l_qseq);
    // Tests per sam format spec
    const bool hasSeq = lq > 0;
    const bool hasQual = hasSeq && bam_get_qual (br_b1)[0] != 0xFF;
    if (hasSeq) {
      pf.seq.emplace (lq, ' ');

      const uint8_t* br_qs = bam_get_seq (br_b1);
      for (size_t j = 0; j < lq; ++j) {
        (*pf.seq)[j] = seq_nt16_str[bam_seqi (br_qs, j)];
      }
      if (!br_p1->is_del) {
        pf.base = (*pf.seq)[static_cast<size_t> (br_p1->qpos)];
      }
    }
    if (hasQual) {
      pf.rawQual.emplace (lq);
      const uint8_t* br_qq = bam_get_qual (br_b1);
      for (size_t j = 0; j < lq; ++j) {
        (*pf.rawQual)[j] = br_qq[j];
      }
      if (!br_p1->is_del) {
        pf.baseQual = br_qq[br_p1->qpos];
      }
    }
    // else these fields are nullopt
  }

  {
    /* stringify cigar */
    APB_ASSERT (br_cig != nullptr);
    pf.cig = [&br_cig, &nCig]() {
      std::string out;
      for (size_t opi = 0; opi < nCig; opi++) {
        const auto cigel = br_cig[opi];
        out += std::to_string (bam_cigar_oplen (cigel));
        out += bam_cigar_opchr (cigel);
      }
      return out;
    }();

    pf.end = pf.start + bam_cigar2rlen (static_cast<int> (nCig), br_cig);
  }

  {
    /* aux to json; left empty if no aux tags present */
    auto& auxJson = pf.auxJson;
    auxJson.clear();
    const uint8_t* br_dataEnd = br_b1->data + br_b1->l_data;
    const uint8_t* br_aux1 = bam_aux_first (br_b1);
    if (br_aux1 != NULL) {
      auxJson += '{';
      for (; br_aux1 != nullptr;) {
        auto tagEntry = aux1_to_json (br_aux1, br_dataEnd);
        if (!tagEntry) {
          return false;
        }
        auxJson += *tagEntry;
        br_aux1 = bam_aux_next (br_b1, br_aux1);
        if (br_aux1 == NULL) {
          break;
        }
        auxJson += ',';
      }
      auxJson += '}';
    }
  }

  APB_ASSERT (pf.valid(), pf.to_string());
  return true;
}

insert_pileup_status_t apb_insert_pileup (
    sqlite3* conn, std::span<const bam_pileup1_t> plp, sqlite3_int64 alnId,
    const std::function<const char*(int)>& mtid2name
)
{
  sqlite3_stmt* o_stmt;
  if (const auto rc = sqlite3_prepare_v2 (
          conn, schema::sqlInsertReads.data(),
          static_cast<int> (schema::sqlInsertReads.size()), &o_stmt, NULL
      );
      rc != SQLITE_OK) {
    return {insert_pileup_status_t::sqlFail, rc};
  }
  Defer stmt_cleanup ([&o_stmt]() { return sqlite3_finalize (o_stmt); });

  // manually begin/commit transaction.
  if (const auto rc = exec_sql (conn, "BEGIN;"); rc != SQLITE_OK) {
    return {insert_pileup_status_t::sqlFail, rc};
  }

  for (size_t i = 0; i < plp.size(); ++i) {
    apb_pileup_t readI;
    const auto* p1 = &plp[i];

    const char* mtidName = NULL;
    {
      /* stringify mtid, if available */
      // '=' if same contig as this read, per SAM RNEXT convention;
      // NULL if no mate reference (core.mtid < 0).
      const auto* b1 = p1->b;
      if (b1->core.mtid >= 0) {
        // NOTE: in the case where tid2name
        // fails, null recorded in database.
        // Hence failure not checked.
        mtidName = (b1->core.mtid == b1->core.tid)
                       ? "="
                       : mtid2name (b1->core.mtid);
      }
      else {
        mtidName = NULL;
      }
    }

    // FIXME: return qname of offending read on failure
    // NOTE: fill_fields asserts internally
    if (!fill_fields (readI, p1, mtidName)) {
      return insert_pileup_status_t{
          .code = insert_pileup_status_t::auxParseFail,
          .rc = std::nullopt /* no specific code */
      };
    }

#define TRY_BIND(bind_fn)                           \
  do {                                              \
    if (const auto rc = bind_fn; rc != SQLITE_OK) { \
      return {insert_pileup_status_t::sqlFail, rc}; \
    }                                               \
  } while (false)

    /* bind statement */
    // CONVERTS FROM 0-INDEXED PileupFields TO 1-INDEXED DB REPRESENTATION
    // INSERTION ORDER TIED TO SCHEMA; BE CAREFUL! (schema.hpp)
    int col = 1;

    TRY_BIND (sqlite3_bind_text (
        o_stmt, col++, readI.qName.data(),
        static_cast<int> (readI.qName.size()), SQLITE_TRANSIENT
    ));
    TRY_BIND (sqlite3_bind_int (o_stmt, col++, readI.flag));
    TRY_BIND (sqlite3_bind_int64 (o_stmt, col++, readI.start + 1));
    TRY_BIND (sqlite3_bind_int64 (o_stmt, col++, readI.end));
    TRY_BIND (sqlite3_bind_int (o_stmt, col++, readI.mapQ));

    if (readI.base) {
      TRY_BIND (sqlite3_bind_text (
          o_stmt, col++, &(*readI.base), 1, SQLITE_TRANSIENT
      ));
    }
    else {
      TRY_BIND (sqlite3_bind_null (o_stmt, col++));
    }

    if (readI.baseQual) {
      TRY_BIND (sqlite3_bind_int (o_stmt, col++, *readI.baseQual));
    }
    else {
      TRY_BIND (sqlite3_bind_null (o_stmt, col++));
    }

    TRY_BIND (sqlite3_bind_int64 (o_stmt, col++, readI.qPos + 1));
    TRY_BIND (sqlite3_bind_int (o_stmt, col++, readI.indel));
    TRY_BIND (
        sqlite3_bind_int (o_stmt, col++, static_cast<int> (readI.isDel))
    );
    TRY_BIND (
        sqlite3_bind_int (o_stmt, col++, static_cast<int> (readI.isHead))
    );
    TRY_BIND (
        sqlite3_bind_int (o_stmt, col++, static_cast<int> (readI.isTail))
    );
    TRY_BIND (sqlite3_bind_int (
        o_stmt, col++, static_cast<int> (readI.isRefSkip)
    ));
    TRY_BIND (sqlite3_bind_text (
        o_stmt, col++, readI.cig.data(),
        static_cast<int> (readI.cig.size()), SQLITE_TRANSIENT
    ));

    if (readI.seq) {
      TRY_BIND (sqlite3_bind_text (
          o_stmt, col++, readI.seq->data(),
          static_cast<int> (readI.seq->size()), SQLITE_TRANSIENT
      ));
    }
    else {
      TRY_BIND (sqlite3_bind_null (o_stmt, col++));
    }

    if (readI.rawQual) {
      TRY_BIND (sqlite3_bind_blob (
          o_stmt, col++, readI.rawQual->data(),
          static_cast<int> (readI.rawQual->size()), SQLITE_TRANSIENT
      ));
    }
    else {
      TRY_BIND (sqlite3_bind_null (o_stmt, col++));
    }

    if (readI.mtidName) {
      TRY_BIND (sqlite3_bind_text (
          o_stmt, col++, readI.mtidName->data(),
          static_cast<int> (readI.mtidName->size()), SQLITE_TRANSIENT
      ));
    }
    else {
      TRY_BIND (sqlite3_bind_null (o_stmt, col++));
    }

    if (readI.mStart) {
      TRY_BIND (
          sqlite3_bind_int64 (o_stmt, col++, readI.mStart.value() + 1)
      );
    }
    else {
      TRY_BIND (sqlite3_bind_null (o_stmt, col++));
    }

    if (readI.auxJson.empty()) {
      TRY_BIND (sqlite3_bind_null (o_stmt, col++));
    }
    else {
      TRY_BIND (sqlite3_bind_text (
          o_stmt, col++, readI.auxJson.data(),
          static_cast<int> (readI.auxJson.size()), SQLITE_TRANSIENT
      ));
    }

    TRY_BIND (sqlite3_bind_blob (
        o_stmt, col++, readI.rawCig.data(),
        static_cast<int> (readI.nCig << 2), SQLITE_TRANSIENT
    ));
    TRY_BIND (
        sqlite3_bind_int (o_stmt, col++, static_cast<int> (readI.nCig))
    );
    TRY_BIND (sqlite3_bind_int64 (o_stmt, col++, alnId));

#undef TRY_BIND

    if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_DONE) {
      return {insert_pileup_status_t::sqlFail, rc};
    }
    sqlite3_reset (o_stmt);  // rc mirrors the step already checked above
    sqlite3_clear_bindings (o_stmt);  // cannot fail per sqlite3 docs
  }

  if (const auto rc = exec_sql (conn, "COMMIT;"); rc != SQLITE_OK) {
    return {insert_pileup_status_t::sqlFail, rc};
  }
  return {};
};

[[nodiscard]] int apb_insert_locus (
    sqlite3* conn, const std::string& contigName, int64_t pos,
    GenomicSpan span, std::optional<std::string_view> ref
)
{
  /*
    insert the pileup locus into the database's single metadata row.
    Uses automatic transaction handling, not necessary to begin/end transaction.

    CONVERTS FROM 0-INDEXED HTSLIB DATA TO 1-INDEXED INTERNAL REPRESENTATION
  */
  APB_ASSERT (conn != nullptr);
  APB_ASSERT (!contigName.empty());
  APB_ASSERT (pos >= 0, pos);
  if (ref) {
    APB_ASSERT (!ref->empty());
  }

  uint16_t nAln;
  {
    sqlite3_stmt* o_stmt;
    TRY_OK_INT (sqlite3_prepare_v2 (
        conn, "SELECT COUNT(*) FROM alignment_files;", -1, &o_stmt, NULL
    ));
    if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_DONE) {
      return rc;
    }
    nAln = static_cast<uint16_t> (sqlite3_column_int (o_stmt, 0));
  }

  sqlite3_stmt* o_stmt;
  TRY_OK_INT (sqlite3_prepare_v2 (
      conn, schema::sqlInsertMetadata.data(),
      static_cast<int> (schema::sqlInsertMetadata.size()), &o_stmt, NULL
  ));
  Defer stmt_cleanup ([&o_stmt]() { sqlite3_finalize (o_stmt); });

  // NOTE: TIED TO SCHEMA ORDER. BE CAREFUL!
  int col = 1;

  TRY_OK_INT (sqlite3_bind_text (
      o_stmt, col++, contigName.c_str(), -1, SQLITE_TRANSIENT
  ));
  TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col++, pos + 1));
  TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col++, span.start + 1));
  TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col++, span.end));
  if (!ref) {
    TRY_OK_INT (sqlite3_bind_null (o_stmt, col++));
  }
  else {
    TRY_OK_INT (sqlite3_bind_text (
        o_stmt, col++, ref->data(), static_cast<int> (ref->size()),
        SQLITE_TRANSIENT
    ));
  }
  TRY_OK_INT (sqlite3_bind_int64 (o_stmt, col, nAln));

  if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_DONE) {
    return rc;
  }
  return SQLITE_OK;
}

std::expected<int, int> apb_insert_aln (
    sqlite3* conn, std::string_view alnPath, bool hasContig, int64_t nPlp
)
{
  APB_ASSERT (conn != nullptr);
  APB_ASSERT (nPlp >= 0, nPlp);
  APB_ASSERT (!alnPath.empty());

  sqlite3_stmt* o_fileInsertStmt;
  Defer file_stmt_cleanup ([&o_fileInsertStmt]() {
    sqlite3_finalize (o_fileInsertStmt);
  });
  TRY_OK_EXP (sqlite3_prepare_v2 (
      conn, schema::sqlInsertFile.data(),
      static_cast<int> (schema::sqlInsertFile.size()), &o_fileInsertStmt,
      NULL
  ));
  TRY_OK_EXP (sqlite3_bind_text (
      o_fileInsertStmt, 1, alnPath.data(),
      static_cast<int> (alnPath.size()), SQLITE_TRANSIENT
  ));
  TRY_OK_EXP (
      sqlite3_bind_int (o_fileInsertStmt, 2, static_cast<bool> (hasContig))
  );
  TRY_OK_EXP (sqlite3_bind_int64 (o_fileInsertStmt, 3, nPlp));
  if (const auto rc = sqlite3_step (o_fileInsertStmt); rc != SQLITE_DONE) {
    return rc;
  }
  file_stmt_cleanup.invoke();
  return sqlite3_last_insert_rowid (conn);
}


/* --- TESTS --- */
#ifndef DOCTEST_CONFIG_DISABLE

#include <filesystem>
#include <limits>

#include "doctest.h"
#include "fixtures.hpp"

/* ---- TESTS append_json_escaped ---- */

TEST_CASE ("append_json_escaped escapes per JSON string rules")
{
  SUBCASE ("quote and backslash")
  {
    std::string out;
    const std::string in = "A\"B";
    hts2sql::append_json_escaped (in.data(), in.size(), out);
    CHECK (out == "A\\\"B");

    out.clear();
    const std::string in2 = "A\\B";
    hts2sql::append_json_escaped (in2.data(), in2.size(), out);
    CHECK (out == "A\\\\B");
  }

  SUBCASE ("control characters escape to \\u00XX")
  {
    std::string out;
    const char in[] = {static_cast<char> (0x01)};
    hts2sql::append_json_escaped (in, 1, out);
    CHECK (out == "\\u0001");

    out.clear();
    const char in2[] = {static_cast<char> (0x1f)};
    hts2sql::append_json_escaped (in2, 1, out);
    CHECK (out == "\\u001f");
  }

  SUBCASE (
      "printable ASCII and non-ASCII bytes pass through "
      "unchanged"
  )
  {
    std::string out;
    const std::string in = "hello";
    hts2sql::append_json_escaped (in.data(), in.size(), out);
    CHECK (out == "hello");

    out.clear();
    const char in2[] = {static_cast<char> (0xC3)};
    hts2sql::append_json_escaped (in2, 1, out);
    REQUIRE (out.size() == 1);
    CHECK (static_cast<unsigned char> (out[0]) == 0xC3);
  }

  SUBCASE ("empty input appends nothing")
  {
    std::string out = "unchanged";
    hts2sql::append_json_escaped (nullptr, 0, out);
    CHECK (out == "unchanged");
  }
}

// ---- TESTS aux1_to_json ---- //
TEST_CASE ("aux1_to_json escapes quote and backslash in string tags")
{
  SUBCASE ("string value containing '\"' and '\\' gets escaped")
  {
    uint8_t buf[] = {'Z', 'Z', 'Z', 'a', '"', 'b', '\\', 'c', '\0'};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);

    std::string expectedVal = "a";
    expectedVal += '\\';
    expectedVal += '"';
    expectedVal += "b";
    expectedVal += '\\';
    expectedVal += '\\';
    expectedVal += "c";
    std::string expected = "\"ZZ\":\"";
    expected += expectedVal;
    expected += "\"";
    CHECK (*r == expected);
  }
}

TEST_CASE (
    "aux1_to_json handles zero-, one-, and multi-element 'B' "
    "arrays"
)
{
  SUBCASE ("zero elements")
  {
    uint8_t buf[] = {'X', 'Y', 'B', 'c', 0, 0, 0, 0};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XY\":[]");
  }

  SUBCASE ("single element")
  {
    uint8_t buf[] = {'X', 'Y', 'B', 'c', 1, 0, 0, 0, 5};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XY\":[5]");
  }

  SUBCASE ("three elements")
  {
    uint8_t buf[] = {'X', 'Y', 'B', 'c', 3, 0, 0, 0, 1, 2, 3};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XY\":[1,2,3]");
  }
}

TEST_CASE ("aux1_to_json formats float, hex and awkward string tags")
{
  const auto floatTag = [] (float f) {
    uint8_t buf[7] = {'X', 'F', 'f'};
    std::memcpy (buf + 3, &f, sizeof (f));
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    return *r;
  };

  SUBCASE ("finite float") { CHECK (floatTag (1.5f) == "\"XF\":1.5"); }

  SUBCASE ("float array")
  {
    uint8_t buf[] = {'X', 'F', 'B', 'f', 2, 0, 0,  0,
                     0,   0,   192, 63,  0, 0, 32, 64};  // 1.5f, 2.5f
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XF\":[1.5,2.5]");
  }

  SUBCASE ("non-finite float becomes null")
  {
    CHECK (
        floatTag (std::numeric_limits<float>::quiet_NaN()) == "\"XF\":null"
    );
    CHECK (
        floatTag (std::numeric_limits<float>::infinity()) == "\"XF\":null"
    );
    CHECK (
        floatTag (-std::numeric_limits<float>::infinity()) == "\"XF\":null"
    );
  }

  SUBCASE ("non-finite in float array becomes null")
  {
    const float v[] = {
        1.5f, std::numeric_limits<float>::quiet_NaN(),
        -std::numeric_limits<float>::infinity()
    };
    uint8_t buf[8 + sizeof (v)] = {'X', 'F', 'B', 'f', 3, 0, 0, 0};
    std::memcpy (buf + 8, v, sizeof (v));
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"XF\":[1.5,null,null]");
  }

  SUBCASE ("empty string")
  {
    uint8_t buf[] = {'Z', 'E', 'Z', '\0'};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"ZE\":\"\"");
  }

  SUBCASE ("hex string")
  {
    uint8_t buf[] = {'H', 'X', 'H', '1', 'A', '\0'};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"HX\":\"1A\"");
  }

  SUBCASE ("control character in string")
  {
    uint8_t buf[] = {'C', 'C', 'Z', 'a', 0x01, 'b', '\0'};
    auto r = hts2sql::aux1_to_json (buf + 2, buf + sizeof (buf));
    REQUIRE (r);
    CHECK (*r == "\"CC\":\"a\\u0001b\"");
  }
}

namespace test {

// alignment_files row that per-record inserts reference via path_id.
constexpr sqlite3_int64 k_testFileId = 1;

void seed_file (sqlite3* conn)
{
  const char* sql =
      "INSERT INTO alignment_files (id, path, has_contig, has_reads) "
      "VALUES (1, 'test.bam', 1, 1);";
  REQUIRE (
      sqlite3_exec (conn, sql, nullptr, nullptr, nullptr) == SQLITE_OK
  );
}

}  // namespace test

/* ---- TESTS seed_reads (shared fixture) ---- */

namespace test {

// seed a fresh db with locus metadata and three reads
void seed_reads (
    sqlite3* conn, int16_t nAln, std::optional<std::string> refSlice
)
{
  seed_file (conn);
  REQUIRE (
      hts2sql::insert_metadata (
          conn, "chr1", 149, GenomicSpan{.start = 99, .end = 200},
          std::move (refSlice), nAln
      ) == SQLITE_OK
  );

  auto stmt = hts2sql::prepare_insert_reads_stmt (conn);

  auto row = [] (std::string qName, uint16_t flag, uint8_t mapQ,
                 hts_pos_t start, hts_pos_t end) {
    hts2sql::PileupFields pf{};
    pf.qName = std::move (qName);
    pf.flag = flag;
    pf.start = start - 1;  // 0-indexed input -> db start
    pf.end = end;  // unshifted -> db end
    pf.mapQ = mapQ;
    pf.base = 'A';
    pf.baseQual = 30;
    pf.qPos = 0;
    pf.indel = 0;
    pf.isDel = false;
    pf.isHead = false;
    pf.isTail = false;
    pf.isRefSkip = false;
    pf.cig = "1M";
    pf.seq = "A";
    pf.rawQual = std::vector<uint8_t> (1, 30);
    pf.auxJson = "";
    pf.rawCig = {static_cast<uint32_t> (bam_cigar_gen (1, BAM_CMATCH))};
    pf.nCig = pf.rawCig.size();
    return pf;
  };

  // (qName, flag, mapQ, start, end) -- all satisfy
  // 100 <= start <= 150 <= end <= 200.
  const std::vector<hts2sql::PileupFields> rows{
      row ("readA", 99, 60, 100, 170),
      row ("readB", 4, 10, 120, 190),
      row ("readC", 99, 45, 140, 200),
  };
  for (const auto& pf : rows) {
    hts2sql::bind_read_data (stmt, pf, k_testFileId);
    REQUIRE (sqlite3_step (stmt) == SQLITE_DONE);
    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
  }
}

}  // namespace test

/* ---- TESTS insert_pileup ---- */

namespace test {

// Hands pre-parsed records, in order, to htslib's pileup engine.
struct RecordFeed {
  const std::vector<bam1_t*>* recs;
  size_t next = 0;
};
extern "C" {
int next_fed_record (void* data, bam1_t* b)
{
  auto* feed = static_cast<RecordFeed*> (data);
  if (feed->next >= feed->recs->size()) {
    return -1;
  }
  return bam_copy1 (b, (*feed->recs)[feed->next++]) == NULL ? -2 : 0;
}
}

// Expected stored values. 1-based, as in the db.
struct ExpectedRead {
  int64_t start, end;
  std::string cigar;
  int ncig;
  std::optional<int> qpos = std::nullopt;  // not checked if nullopt
  std::optional<char> base = std::nullopt;  // nullopt: expect NULL
  std::optional<int> basequal = std::nullopt;  // nullopt: expect NULL
  bool hasSeq = true;
  bool hasQual = true;
  // not checked if nullopt
  std::optional<int> indel = std::nullopt;
  std::optional<int> isDel = std::nullopt;
  std::optional<int> isHead = std::nullopt;
  std::optional<int> isTail = std::nullopt;
  std::optional<int> isRefSkip = std::nullopt;
  std::optional<std::string> mtid = std::nullopt;  // nullopt: expect NULL
  std::optional<int64_t> mstart = std::nullopt;  // nullopt: expect NULL
};

}  // namespace test

TEST_CASE ("insert_pileup stores edge-case reads piled up by htslib")
{
  // Reads are parsed from SAM text and piled up by htslib itself, so
  // qpos/is_del/is_head etc. are as the real program sees them.
  // Pileup position is chr1:100 (1-based).
  // Seq/qual are prefixes of the strings below, so the base and quality
  // at each index are distinct: basequal at index i is 32 + i.
  const std::string kSeq = "ACGTTGCAAGCT";
  const std::string kQual = "ABCDEFGHIJKL";
  const auto seq = [&] (size_t n) { return kSeq.substr (0, n); };
  const auto qual = [&] (size_t n) { return kQual.substr (0, n); };
  const auto samLine = [] (std::string_view qname, int flag, int pos,
                           std::string_view cigar, std::string_view rnext,
                           int pnext, std::string_view sq,
                           std::string_view ql) {
    return fmt::format (
        "{}\t{}\tchr1\t{}\t60\t{}\t{}\t{}\t0\t{}\t{}", qname, flag, pos,
        cigar, rnext, pnext, sq, ql
    );
  };

  struct Case {
    std::string qname;
    std::string sam;
    test::ExpectedRead expect;
  };
  // coordinate sorted, as htslib's pileup requires.
  const std::vector<Case> cases{
      {"normal",
       samLine ("normal", 99, 95, "10M", "chr2", 500, seq (10), qual (10)),
       {.start = 95,
        .end = 104,
        .cigar = "10M",
        .ncig = 1,
        .qpos = 6,
        .base = 'G',
        .basequal = 37,
        .isHead = 0,
        .isTail = 0,
        .mtid = "chr2",
        .mstart = 500}},
      {"deletion",
       samLine ("deletion", 0, 95, "4M2D6M", "*", 0, seq (10), qual (10)),
       {.start = 95,
        .end = 106,
        .cigar = "4M2D6M",
        .ncig = 3,
        .isDel = 1}},
      {"refskip",
       samLine ("refskip", 0, 95, "4M2N6M", "*", 0, seq (10), qual (10)),
       {.start = 95,
        .end = 106,
        .cigar = "4M2N6M",
        .ncig = 3,
        .isRefSkip = 1}},
      {"insafter",
       samLine ("insafter", 0, 95, "6M2I4M", "*", 0, seq (12), qual (12)),
       {.start = 95,
        .end = 104,
        .cigar = "6M2I4M",
        .ncig = 3,
        .qpos = 6,
        .base = 'G',
        .basequal = 37,
        .indel = 2}},
      {"eqx",
       samLine ("eqx", 0, 95, "3=1X6=", "*", 0, seq (10), qual (10)),
       {.start = 95,
        .end = 104,
        .cigar = "3=1X6=",
        .ncig = 3,
        .qpos = 6,
        .base = 'G',
        .basequal = 37}},
      {"qualstar",
       samLine ("qualstar", 0, 96, "8M", "*", 0, seq (8), "*"),
       {.start = 96,
        .end = 103,
        .cigar = "8M",
        .ncig = 1,
        .qpos = 5,
        .base = 'T',
        .hasQual = false,
        .mtid = std::nullopt}},
      {"hardclip",
       samLine ("hardclip", 0, 96, "2H8M3H", "*", 0, seq (8), qual (8)),
       {.start = 96,
        .end = 103,
        .cigar = "2H8M3H",
        .ncig = 3,
        .qpos = 5,
        .base = 'T',
        .basequal = 36}},
      {"tail",
       samLine ("tail", 0, 96, "5M", "*", 0, seq (5), qual (5)),
       {.start = 96,
        .end = 100,
        .cigar = "5M",
        .ncig = 1,
        .qpos = 5,
        .base = 'T',
        .basequal = 36,
        .isHead = 0,
        .isTail = 1}},
      {"seqstar",
       samLine ("seqstar", 0, 97, "6M", "*", 0, "*", "*"),
       {.start = 97,
        .end = 102,
        .cigar = "6M",
        .ncig = 1,
        .qpos = 4,
        .hasSeq = false,
        .hasQual = false}},
      {"softclip",
       samLine ("softclip", 0, 98, "3S5M2S", "*", 0, seq (10), qual (10)),
       {.start = 98,
        .end = 102,
        .cigar = "3S5M2S",
        .ncig = 3,
        .qpos = 6,
        .base = 'G',
        .basequal = 37}},
      {"head",
       samLine ("head", 0, 100, "5M", "*", 0, seq (5), qual (5)),
       {.start = 100,
        .end = 104,
        .cigar = "5M",
        .ncig = 1,
        .qpos = 1,
        .base = 'A',
        .basequal = 32,
        .isHead = 1,
        .isTail = 0}},
      {"onebase",
       samLine ("onebase", 0, 100, "1M", "*", 0, "G", "5"),
       {.start = 100,
        .end = 100,
        .cigar = "1M",
        .ncig = 1,
        .qpos = 1,
        .base = 'G',
        .basequal = 20,
        .isHead = 1,
        .isTail = 1}},
  };

  /* parse records */
  const std::string hdrText =
      "@HD\tVN:1.6\tSO:coordinate\n@SQ\tSN:chr1\tLN:1000\n"
      "@SQ\tSN:chr2\tLN:1000\n";
  sam_hdr_t* hdr = sam_hdr_parse (hdrText.size(), hdrText.c_str());
  REQUIRE (hdr != nullptr);
  Defer hdrCleanup ([&]() { sam_hdr_destroy (hdr); });

  std::vector<bam1_t*> recs;
  Defer recsCleanup ([&]() {
    for (auto* r : recs) {
      bam_destroy1 (r);
    }
  });
  kstring_t ks = KS_INITIALIZE;
  Defer ksCleanup ([&]() { ks_free (&ks); });
  for (const auto& c : cases) {
    INFO ("parsing " << c.qname);
    ks.l = 0;
    REQUIRE (kputs (c.sam.c_str(), &ks) >= 0);
    bam1_t* b = bam_init1();
    REQUIRE (b != nullptr);
    recs.push_back (b);
    REQUIRE (sam_parse1 (&ks, hdr, b) >= 0);
  }

  /* pile up and insert */
  sqlite3* conn = setup_db::init();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
  test::seed_file (conn);

  test::RecordFeed feed{.recs = &recs};
  bam_plp_t plp = bam_plp_init (test::next_fed_record, &feed);
  REQUIRE (plp != nullptr);
  Defer plpCleanup ([&]() { bam_plp_destroy (plp); });

  constexpr int64_t pileupPos0 = 99;
  const bam_pileup1_t* br_plp = nullptr;
  int plpTid = -1;
  int nPlp = -1;
  int64_t plpPos = -1;
  bool found = false;
  while ((br_plp = bam_plp64_auto (plp, &plpTid, &plpPos, &nPlp)) !=
         nullptr) {
    if (plpPos < pileupPos0) {
      continue;
    }
    REQUIRE (plpPos == pileupPos0);
    REQUIRE (static_cast<size_t> (nPlp) == cases.size());
    const auto status = hts2sql::insert_pileup (
        conn, br_plp, static_cast<size_t> (nPlp), "chr1",
        test::k_testFileId,
        [hdr] (int tid) { return sam_hdr_tid2name (hdr, tid); }
    );
    REQUIRE (status.code == hts2sql::InsertPileupStatus::success);
    found = true;
    break;
  }
  REQUIRE (found);

  /* check stored rows */
  sqlite3_stmt* o_stmt = nullptr;
  REQUIRE (
      sqlite3_prepare_v2 (
          conn, "SELECT * FROM reads;", -1, &o_stmt, NULL
      ) == SQLITE_OK
  );
  Defer stmtCleanup ([&]() { sqlite3_finalize (o_stmt); });

  using Col = schema::UserReadViewSelect;
  const auto isNull = [&] (int col) {
    return sqlite3_column_type (o_stmt, col) == SQLITE_NULL;
  };
  const auto asInt = [&] (int col) {
    return sqlite3_column_int64 (o_stmt, col);
  };
  const auto asText = [&] (int col) -> std::optional<std::string> {
    if (isNull (col)) {
      return std::nullopt;
    }
    return std::string (
        reinterpret_cast<const char*> (sqlite3_column_text (o_stmt, col)),
        static_cast<size_t> (sqlite3_column_bytes (o_stmt, col))
    );
  };

  size_t nRows = 0;
  while (sqlite3_step (o_stmt) == SQLITE_ROW) {
    ++nRows;
    const auto qname = asText (Col::qname);
    REQUIRE (qname.has_value());
    INFO ("read " << *qname);
    const auto it =
        std::find_if (cases.begin(), cases.end(), [&] (const Case& c) {
          return c.qname == *qname;
        });
    REQUIRE (it != cases.end());
    const auto& e = it->expect;

    // Expected and stored values are rendered in parallel, field by
    // field, and compared once per read.
    std::string want, got;
    const auto field = [&] (
                           std::string_view name, const auto& wantVal,
                           const auto& gotVal
                       ) {
      want += fmt::format (" {}={}", name, wantVal);
      got += fmt::format (" {}={}", name, gotVal);
    };
    const auto optText = [&] (int col) {
      return asText (col).value_or ("NULL");
    };
    const auto optInt = [&] (const auto& v) {
      return v ? std::to_string (*v) : std::string ("NULL");
    };
    const auto setOrNull = [&] (bool set) { return set ? "set" : "NULL"; };

    field ("start", e.start, asInt (Col::start));
    field ("end", e.end, asInt (Col::end));
    field ("cigar", e.cigar, optText (Col::cigar));
    field ("ncig", e.ncig, asInt (Col::ncig));
    field (
        "base", e.base ? std::string (1, *e.base) : std::string ("NULL"),
        optText (Col::base)
    );
    field (
        "basequal", optInt (e.basequal),
        isNull (Col::basequal) ? std::string ("NULL")
                               : std::to_string (asInt (Col::basequal))
    );
    field ("seq", setOrNull (e.hasSeq), setOrNull (!isNull (Col::seq)));
    field ("qual", setOrNull (e.hasQual), setOrNull (!isNull (Col::qual)));
    field ("mtid", e.mtid.value_or ("NULL"), optText (Col::mtid));
    field (
        "mstart", optInt (e.mstart),
        isNull (Col::mstart) ? std::string ("NULL")
                             : std::to_string (asInt (Col::mstart))
    );
    field (
        "tags", "NULL", optText (Col::tags)
    );  // no aux tags in any read
    // only where the case specifies a value
    if (e.qpos) {
      field ("qpos", *e.qpos, asInt (Col::qpos));
    }
    if (e.indel) {
      field ("indel", *e.indel, asInt (Col::indel));
    }
    if (e.isDel) {
      field ("is_del", *e.isDel, asInt (Col::is_del));
    }
    if (e.isHead) {
      field ("is_head", *e.isHead, asInt (Col::is_head));
    }
    if (e.isTail) {
      field ("is_tail", *e.isTail, asInt (Col::is_tail));
    }
    if (e.isRefSkip) {
      field ("is_refskip", *e.isRefSkip, asInt (Col::is_refskip));
    }
    CHECK (got == want);
  }
  CHECK (nRows == cases.size());
}

/* ---- TESTS dump_to_disk / load_from_disk ---- */

namespace test {

std::filesystem::path temp_db_path (std::string_view name)
{
  return std::filesystem::temp_directory_path() / name;
}

// Every column of every row of `sql`'s result, as one string.
std::string digest (sqlite3* conn, const char* sql)
{
  sqlite3_stmt* o_stmt = nullptr;
  REQUIRE (sqlite3_prepare_v2 (conn, sql, -1, &o_stmt, NULL) == SQLITE_OK);
  Defer stmtCleanup ([&]() { sqlite3_finalize (o_stmt); });
  std::string out;
  while (sqlite3_step (o_stmt) == SQLITE_ROW) {
    for (int i = 0; i < sqlite3_column_count (o_stmt); ++i) {
      if (sqlite3_column_type (o_stmt, i) == SQLITE_NULL) {
        out += "NULL";
      }
      else if (sqlite3_column_type (o_stmt, i) == SQLITE_BLOB) {
        const auto* br_blob =
            static_cast<const uint8_t*> (sqlite3_column_blob (o_stmt, i));
        for (int j = 0; j < sqlite3_column_bytes (o_stmt, i); ++j) {
          out += fmt::format ("{:02x}", br_blob[j]);
        }
      }
      else {
        out += reinterpret_cast<const char*> (
            sqlite3_column_text (o_stmt, i)
        );
      }
      out += '|';
    }
    out += '\n';
  }
  return out;
}

}  // namespace test

TEST_CASE ("dump_to_disk then load_from_disk round-trips a database")
{
  const auto path = test::temp_db_path ("apb-test-roundtrip.db");
  std::filesystem::remove (path);
  Defer removeFile ([&]() { std::filesystem::remove (path); });

  sqlite3* conn = setup_db::init();
  Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
  test::seed_reads (conn, 2, std::make_optional<std::string> ("ACGTACGT"));
  REQUIRE (
      sqlite3_exec (
          conn,
          "INSERT INTO alignment_files (path, has_contig, has_reads) "
          "VALUES ('other.bam', 0, 0);",
          nullptr, nullptr, nullptr
      ) == SQLITE_OK
  );

  REQUIRE (
      setup_db::dump_to_disk (conn, path.string()).code ==
      setup_db::DiskDumpStatus::success
  );
  auto loadRet = setup_db::load_from_disk (path.string());
  REQUIRE (loadRet);
  sqlite3* loaded = *loadRet;
  Defer closeLoaded ([&]() { sqlite3_close_v2 (loaded); });

  CHECK (
      test::digest (loaded, "SELECT * FROM reads ORDER BY id;") ==
      test::digest (conn, "SELECT * FROM reads ORDER BY id;")
  );
  CHECK (
      test::digest (
          loaded, "SELECT * FROM alignment_files ORDER BY id;"
      ) ==
      test::digest (conn, "SELECT * FROM alignment_files ORDER BY id;")
  );
  CHECK (
      test::digest (loaded, "SELECT * FROM locus_metadata;") ==
      test::digest (conn, "SELECT * FROM locus_metadata;")
  );
}

TEST_CASE ("load_from_disk reports a typed error for unusable files")
{
  const auto path = test::temp_db_path ("apb-test-unusable.db");
  std::filesystem::remove (path);
  Defer removeFile ([&]() { std::filesystem::remove (path); });

  SUBCASE ("nonexistent file")
  {
    const auto ret = setup_db::load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().code == setup_db::LoadErr::openFail);
  }

  SUBCASE ("sqlite file with a different schema")
  {
    sqlite3* other = nullptr;
    REQUIRE (
        sqlite3_open_v2 (
            path.c_str(), &other,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL
        ) == SQLITE_OK
    );
    REQUIRE (
        sqlite3_exec (other, "CREATE TABLE x (a);", NULL, NULL, NULL) ==
        SQLITE_OK
    );
    sqlite3_close_v2 (other);

    const auto ret = setup_db::load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().code == setup_db::LoadErr::schemaMismatch);
  }

  SUBCASE ("apb database with no locus metadata")
  {
    sqlite3* conn = setup_db::init();
    Defer closeConn ([&]() { sqlite3_close_v2 (conn); });
    REQUIRE (
        setup_db::dump_to_disk (conn, path.string()).code ==
        setup_db::DiskDumpStatus::success
    );

    const auto ret = setup_db::load_from_disk (path.string());
    REQUIRE_FALSE (ret);
    CHECK (ret.error().code == setup_db::LoadErr::contentCorrupt);
  }
}

#endif  // DOCTEST_CONFIG_DISABLE
