#include "apb_db_transform_hts.hpp"

#include <htslib/sam.h>

#include <cstring>
#include <optional>
#include <string>

#include "apb_db/apb_db_core.hpp"
#include "utility/apb_assert.hpp"
#include "utility/cleanup.hpp"


/* tag conversion helper */
// converts the first aux tag in `aux` to a json entry.
// `aux` starts at a tag's 2-ch key and may run on past it.
// returns the bytes after the converted tag, or an empty span
// if there are no more tags. On failure to parse, returns nullopt.
[[nodiscard]] static std::optional<std::span<const uint8_t>> aux1_to_json (
    std::span<const uint8_t> aux, std::string* out
)
{
  // smallest tag is 2-ch key, type byte, 1 value byte.
  constexpr size_t minTagLen = 4;
  if (aux.size() < minTagLen) {
    return std::nullopt;
  }

  kstring_t o_kstr;
  ks_initialize (&o_kstr);
  Defer kstrCleanup ([&]() { ks_free (&o_kstr); });

  const uint8_t* key = aux.data();
  const uint8_t type = aux[2];
  const uint8_t* value = aux.data() + 3;
  const uint8_t* auxEnd = aux.data() + aux.size();
  // returns pointer just past the tag
  const uint8_t* tagEnd =
      sam_format_aux1 (key, type, value, auxEnd, &o_kstr);
  if (tagEnd == nullptr) {
    return std::nullopt;
  }
  const auto tagLen = static_cast<size_t> (tagEnd - aux.data());
  const char* br_str = ks_str (&o_kstr);

  /* append key */
  out->clear();
  *out += '"';  // open key quotes
  out->append (br_str, 2);  // 2-ch tag
  *out += '"';  // close
  *out += ':';  // add key-val separator

  /* append val */
  const char typeCh = *(br_str + 3);
  if (typeCh == 'B') {
    // array aux tag
    // "TAG:B:<subtype>" (2 + 1 + 2 + 1 = 6 chars);
    constexpr auto arrayTagPrefixLen = 6;
    *out += '[';  // open JSON array
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
          *out += ',';
        }
        // JSON has no nan/inf (finite values end in a digit)
        if (tokLen > 0 &&
            (isdigit (static_cast<unsigned char> (tok[tokLen - 1])) !=
             0)) {
          out->append (tok, tokLen);
        }
        else {
          *out += "null";
        }
        firstTok = false;
      }
    }
    *out += ']';  // close JSON array
  }
  else {
    // non-array tag
    constexpr auto tagPrefixLen = 5;
    const auto* payloadStartPtr = br_str + tagPrefixLen;
    const size_t payloadLen = ks_len (&o_kstr) - tagPrefixLen;
    switch (typeCh) {
    case 'A':
    case 'Z':
    case 'H':
      // payload as string
      *out += '"';
      for (size_t i = 0; i < payloadLen; ++i) {
        const auto ch = static_cast<unsigned char> (payloadStartPtr[i]);
        switch (ch) {
        case '"':
          *out += R"txt(\")txt";
          break;
        case '\\':  // one backslash (has to be escaped in cpp src)
          *out += R"txt(\\)txt";
          break;
        default:
          if (ch < 0x20) {
            // json forbids raw control chars
            constexpr char hexDigits[] = "0123456789abcdef";
            *out += "\\u00";
            *out += hexDigits[ch >> 4];
            *out += hexDigits[ch & 0xF];
          }
          else {
            *out += static_cast<char> (ch);
          }
        }
      }
      *out += '"';
      break;
    default:
      // payload as numeric; JSON has no nan/inf (finite values
      // end in a digit)
      if (payloadLen > 0 &&
          (isdigit (
               static_cast<unsigned char> (payloadStartPtr[payloadLen - 1])
           ) != 0)) {
        out->append (payloadStartPtr, payloadLen);
      }
      else {
        *out += "null";
      }
      break;
    }
  }

  return aux.subspan (tagLen);
}


static bool valid (const apb_pileup1_t rec) { return rec.qPos_0ho >= 0; }
static bool valid (const apb_row1_t& rec)
{
  // NOTE: there's a lot more we could check...
  const bool startOk = rec.start_0ho >= 0;
  const bool endOk = !rec.end_0ho || *rec.end_0ho > rec.start_0ho;
  const bool seqQualOk =
      !rec.seq || !rec.qual || rec.seq->size() == rec.qual->size();
  const bool cigOk = !rec.cigBlob.empty();
  const bool plpOk = !rec.plp || valid (*rec.plp);
  return startOk && endOk && seqQualOk && cigOk && plpOk;
}

// fill the non-pileup fields of *out from br_b1.
// Expects *out reset; conditionally set fields are not cleared.
[[nodiscard]] static std::expected<void, transform_err_t> bam1_to_row1 (
    const bam1_t* br_b1, const std::function<const char*(int)>& mtid2name,
    apb_row1_t* out
)
{
  auto& r1 = *out;
  const auto nCig = br_b1->core.n_cigar;
  const auto* br_cig = bam_get_cigar (br_b1);

  r1.start_0ho = br_b1->core.pos;
  if (br_b1->core.mpos >= 0) {  // <0 == unaligned (or no mate)
    r1.mStart_0ho = br_b1->core.mpos;
  }

  // NOTE: qname null terminated,
  // so assignment safe.
  r1.qName = bam_get_qname (br_b1);
  r1.flag = br_b1->core.flag;
  if (br_b1->core.qual != 255) {  // 255 == unavailable, per SAM spec
    r1.mapQ = br_b1->core.qual;
  }
  r1.cigBlob = {br_cig, br_cig + nCig};

  /* stringify mtid, if available */
  // real contig name; the reads view renders '='.
  // NULL if no mate reference (core.mtid < 0).
  if (mtid2name && br_b1->core.mtid >= 0) {
    const auto* name = mtid2name (br_b1->core.mtid);
    if (name != nullptr) {
      r1.mtidName = name;
    }
  }

  {
    /* stringify seq, qual */
    const auto lq = static_cast<size_t> (br_b1->core.l_qseq);
    // Tests per sam format spec
    const bool hasSeq = lq > 0;
    const bool hasQual = hasSeq && bam_get_qual (br_b1)[0] != 0xFF;
    if (hasSeq) {
      r1.seq.emplace (lq, ' ');

      const uint8_t* br_qs = bam_get_seq (br_b1);
      for (size_t j = 0; j < lq; ++j) {
        (*r1.seq)[j] = seq_nt16_str[bam_seqi (br_qs, j)];
      }
    }
    if (hasQual) {
      // clamp to SAM spec max, i.e. '~' as phred+33
      constexpr uint8_t k_maxQual = 93;
      r1.qual.emplace (lq, ' ');
      const uint8_t* br_qq = bam_get_qual (br_b1);
      for (size_t j = 0; j < lq; ++j) {
        (*r1.qual)[j] =
            static_cast<char> (std::min (br_qq[j], k_maxQual) + 33);
      }
    }
    // else these fields are nullopt
  }

  /* end from cigar */
  r1.end_0ho =
      r1.start_0ho + bam_cigar2rlen (static_cast<int> (nCig), br_cig);

  {
    /* aux to json; nullopt if no aux tags present */
    // aux tags run from bam_get_aux to the end of the record
    std::span<const uint8_t> auxRest{
        bam_get_aux (br_b1), br_b1->data + br_b1->l_data
    };
    if (!auxRest.empty()) {
      auto& auxJson = r1.auxJson.emplace ("{");
      static std::string tagEntryBuf;
      while (!auxRest.empty()) {
        const auto next = aux1_to_json (auxRest, &tagEntryBuf);
        if (!next) {
          return std::unexpected (
              transform_err_t{transform_err_t::aux_parse_fail, br_b1}
          );
        }
        auxJson += tagEntryBuf;
        auxRest = *next;
        if (!auxRest.empty()) {
          auxJson += ',';
        }
      }
      auxJson += '}';
    }
  }

  return {};
}

// Convert a bam_pileup1_t
// into the pileup database
// interface type.
std::expected<size_t, transform_err_t> apb_transform_pileup1 (
    std::span<const bam_pileup1_t> plpIn, std::span<apb_row1_t> recordsOut,
    const std::function<const char*(int)>& mtid2name
)
{
  size_t i = 0;
  for (; i < plpIn.size() && i < recordsOut.size(); i++) {
    const auto& p1 = plpIn[i];
    auto& r1 = recordsOut[i];
    r1 = {};  // bam1_to_row1 sets several fields only conditionally
    if (const auto filled = bam1_to_row1 (p1.b, mtid2name, &r1); !filled) {
      return std::unexpected (filled.error());
    }

    r1.plp.emplace();
    r1.plp->qPos_0ho = p1.qpos;
    r1.plp->indel = p1.indel;
    r1.plp->is_del = p1.is_del;
    r1.plp->is_refskip = p1.is_refskip;

    APB_ASSERT (valid (r1));
  }
  return i;
}


std::expected<size_t, transform_err_t> apb_transform_bam1 (
    std::span<const bam1_t* const> samIn, std::span<apb_row1_t> recordsOut,
    const std::function<const char*(int)>& mtid2name, int64_t locusPos_0ho
)
{
  size_t i = 0;
  for (; i < samIn.size() && i < recordsOut.size(); i++) {
    const auto* b1 = samIn[i];
    auto& r1 = recordsOut[i];
    r1 = {};  // clear record, since some fields
              // are only filled conditionally
    if (const auto filled = bam1_to_row1 (b1, mtid2name, &r1); !filled) {
      return std::unexpected (filled.error());
    }

    if (b1->core.pos <= locusPos_0ho && locusPos_0ho < bam_endpos (b1)) {
      // read is aligned to locus.
      // per htslib 1.22 sam.c::resolve_cigar2
      const auto& cig = r1.cigBlob;
      constexpr int opConsumesQuery = 0b01;
      constexpr int opConsumesRef = 0b10;
      const auto lenAt = [&] (size_t j) {
        return static_cast<int> (bam_cigar_oplen (cig[j]));
      };

      // find the ref consuming op containing the locus;
      // iRef/iQuery are that op's start coords
      // FIXME: test against htslib? (which has appropriate fixtures)
      int64_t iRef = b1->core.pos;
      int64_t iQuery = 0;
      size_t iOp = 0;
      for (; iOp < cig.size(); iOp++) {
        const auto opConsumeType =
            bam_cigar_type (bam_cigar_op (cig[iOp]));
        if ((opConsumeType & opConsumesRef) != 0) {
          if (locusPos_0ho < iRef + lenAt (iOp)) {
            break;
          }
          iRef += lenAt (iOp);
        }
        if ((opConsumeType & opConsumesQuery) != 0) {
          iQuery += lenAt (iOp);
        }
      }

      if (iOp < cig.size()) {
        auto& plp = r1.plp.emplace();
        const auto opType = bam_cigar_op (cig[iOp]);

        if (opType == BAM_CDEL || opType == BAM_CREF_SKIP) {
          plp.qPos_0ho = static_cast<int32_t> (iQuery);  // next query base
          plp.is_del = 1;
          plp.is_refskip = opType == BAM_CREF_SKIP;
        }
        else {  // M, =, X
          plp.qPos_0ho =
              static_cast<int32_t> (iQuery + locusPos_0ho - iRef);
        }

        // indel to the next ref base; only at the op's last ref base
        plp.indel = 0;
        if (iRef + lenAt (iOp) - 1 == locusPos_0ho &&
            iOp + 1 < cig.size()) {
          size_t j = iOp + 1;
          const auto nextOpType = bam_cigar_op (cig[j]);
          if (nextOpType == BAM_CDEL && opType != BAM_CDEL) {
            // merge e.g. 1D2D to -3; within a deletion, stays 0
            for (; j < cig.size(); j++) {
              if (bam_cigar_op (cig[j]) != BAM_CDEL) {
                break;
              }
              plp.indel -= lenAt (j);
            }
          }
          else if (nextOpType == BAM_CINS) {
            for (; j < cig.size(); j++) {
              const auto jOpType = bam_cigar_op (cig[j]);
              if (jOpType == BAM_CINS) {
                plp.indel += lenAt (j);
              }
              else if (jOpType != BAM_CPAD) {
                break;
              }
            }
          }
          else if (nextOpType == BAM_CPAD) {
            // insertions after padding, up to the next ref consuming op
            for (j++; j < cig.size(); j++) {
              const auto jOpType = bam_cigar_op (cig[j]);
              if ((bam_cigar_type (jOpType) & opConsumesRef) != 0) {
                break;
              }
              if (jOpType == BAM_CINS) {
                plp.indel += lenAt (j);
              }
            }
          }
        }
      }
    }

    APB_ASSERT (valid (r1));
  }
  return i;
}


/* --- TESTS --- */
#ifndef DOCTEST_CONFIG_DISABLE

#include <limits>

#include "doctest.h"

// ---- TESTS aux1_to_json ---- //
TEST_CASE ("aux1_to_json")
{
  std::string outputBuf;
  SUBCASE ("aux1_to_json formats float, hex and awkward string tags")
  {
    const auto floatTag = [&outputBuf] (float f) {
      // tag format XF:f:<value>
      // (colons in textual representation (SAM) only, not bam)
      uint8_t buf[7] = {'X', 'F', 'f'};
      std::memcpy (buf + 3, &f, sizeof (f));
      REQUIRE (aux1_to_json (buf, &outputBuf).has_value());
    };

    SUBCASE ("finite float")
    {
      floatTag (1.5F);
      CHECK (outputBuf == "\"XF\":1.5");
    }

    SUBCASE ("non-finite float becomes null")
    {
      floatTag (std::numeric_limits<float>::quiet_NaN());
      CHECK (outputBuf == "\"XF\":null");
      floatTag (std::numeric_limits<float>::infinity());
      CHECK (outputBuf == "\"XF\":null");
      floatTag (-std::numeric_limits<float>::infinity());
      CHECK (outputBuf == "\"XF\":null");
    }

    SUBCASE ("float array")
    {
      // tag format XF:B:f:[<value1>, <value2>, ...]
      uint8_t buf[] = {'X', 'F', 'B', 'f', 2, 0, 0,  0,
                       0,   0,   192, 63,  0, 0, 32, 64};  // 1.5f, 2.5f
      REQUIRE (aux1_to_json (buf, &outputBuf).has_value());
      CHECK (outputBuf == "\"XF\":[1.5,2.5]");
    }

    SUBCASE ("non-finite in float array becomes null")
    {
      const float v[] = {
          1.5F, std::numeric_limits<float>::quiet_NaN(),
          -std::numeric_limits<float>::infinity()
      };
      uint8_t buf[8 + sizeof (v)] = {'X', 'F', 'B', 'f', 3, 0, 0, 0};
      std::memcpy (buf + 8, v, sizeof (v));
      REQUIRE (aux1_to_json (buf, &outputBuf).has_value());
      CHECK (outputBuf == "\"XF\":[1.5,null,null]");
    }

    SUBCASE ("empty string")
    {
      uint8_t buf[] = {'Z', 'E', 'Z', '\0'};
      REQUIRE (aux1_to_json (buf, &outputBuf).has_value());
      CHECK (outputBuf == "\"ZE\":\"\"");
    }

    SUBCASE ("hex string")
    {
      uint8_t buf[] = {'H', 'X', 'H', '1', 'A', '\0'};
      REQUIRE (aux1_to_json (buf, &outputBuf).has_value());
      CHECK (outputBuf == "\"HX\":\"1A\"");
    }
  }

  SUBCASE (
      "aux1_to_json handles zero-, one-, and multi-element 'B' "
      "arrays"
  )
  {
    SUBCASE ("zero elements")
    {
      uint8_t input[] = {'X', 'Y', 'B', 'c', 0, 0, 0, 0};
      REQUIRE (
          aux1_to_json (input, &outputBuf).has_value()
      );  // must successfully parse
      CHECK (outputBuf == "\"XY\":[]");
    }

    SUBCASE ("single element")
    {
      uint8_t buf[] = {'X', 'Y', 'B', 'c', 1, 0, 0, 0, 5};
      REQUIRE (aux1_to_json (buf, &outputBuf).has_value());
      CHECK (outputBuf == "\"XY\":[5]");
    }

    SUBCASE ("three elements")
    {
      uint8_t buf[] = {'X', 'Y', 'B', 'c', 3, 0, 0, 0, 1, 2, 3};
      REQUIRE (aux1_to_json (buf, &outputBuf).has_value());
      CHECK (outputBuf == "\"XY\":[1,2,3]");
    }
  }

  SUBCASE ("aux1_to_json returns bytes after the converted tag")
  {
    // XC:C:7 then XA:A:q
    const uint8_t buf[] = {'X', 'C', 'C', 7, 'X', 'A', 'A', 'q'};
    const auto rest = aux1_to_json (buf, &outputBuf);
    REQUIRE (rest.has_value());
    CHECK (outputBuf == "\"XC\":7");
    REQUIRE (rest->size() == 4);
    CHECK (rest->data() == buf + 4);

    const auto end = aux1_to_json (*rest, &outputBuf);
    REQUIRE (end.has_value());
    CHECK (outputBuf == "\"XA\":\"q\"");
    CHECK (end->empty());
  }

  SUBCASE ("aux1_to_json rejects input shorter than a tag")
  {
    const uint8_t buf[] = {'X', 'C', 'C'};
    CHECK_FALSE (aux1_to_json (buf, &outputBuf).has_value());
  }

  SUBCASE ("aux1_to_json escapes Z type string tags per JSON string rules")
  {
    SUBCASE ("quote escaped")
    {
      const uint8_t buf[] = {'A', 'B', 'Z', 'a', '"', 'b', '\0'};
      REQUIRE (aux1_to_json (buf, &outputBuf).has_value());
      CHECK (outputBuf == R"txt("AB":"a\"b")txt");
    }

    SUBCASE ("backslash escaped")
    {
      const uint8_t buf[] = {'A', 'B', 'Z', 'a', '\\', 'b', '\0'};
      REQUIRE (aux1_to_json (buf, &outputBuf).has_value());
      CHECK (outputBuf == R"txt("AB":"a\\b")txt");
    }

    SUBCASE ("control chars escaped")
    {
      const uint8_t buf[] = {'A', 'B', 'Z', 'a', '\t', 'b', '\0'};
      REQUIRE (aux1_to_json (buf, &outputBuf).has_value());
      CHECK (outputBuf == R"txt("AB":"a\u0009b")txt");
    }

    SUBCASE ("other bytes pass through unchanged")
    {
      // 0xC3: non-ascii byte (e.g. utf-8 lead byte)
      const uint8_t buf[] = {'A', 'B', 'Z', 'h', 'i', 0xC3, '\0'};
      REQUIRE (aux1_to_json (buf, &outputBuf).has_value());
      CHECK (outputBuf == "\"AB\":\"hi\xC3\"");
    }
  }
}

// FIXME:
// tests for transform_pileup1
// Previously covered (removed test, apb_db/apb_db_core.cpp): SAM text parsed
// via sam_parse1, piled up by htslib (bam_plp64_auto) at chr1:100,
// inserted, rows read back. Per-read cases:
// - normal: mate on other contig -> mtid name, mstart
// - deletion (4M2D6M): is_del, base/basequal NULL
// - refskip (4M2N6M): is_refskip
// - insafter (6M2I4M): indel = 2
// - eqx (3=1X6=), hardclip (2H8M3H), softclip (3S5M2S): qpos/base
// - qualstar (QUAL '*'): qual NULL, seq set
// - seqstar (SEQ '*'): seq and qual NULL
// - no aux tags: tags NULL
// Seq/qual were distinct per index, so base/basequal checked qpos.
// New since schema v3, also untested:
// - mapq 255 -> mapQ unset
// - mate on same contig -> real contig name, not '='
// - valid() rejections
// FIXME: collect nearby reads in ingest (apb_transform_bam1)
// first, as it may change this pathway

TEST_CASE ("apb_transform_pileup1")
{
  // const auto p1arr = std::to_array<bam_pileup1_t> ({{
  //     .b = bam_init1(),
  //     .qpos = 0,
  //     .indel = 0,
  //     .is_del = false,
  //     .is_head = false,
  //     .is_tail = false,
  //     .is_refskip = false,
  //     // Other fields unused.
  // }});
}

#endif
