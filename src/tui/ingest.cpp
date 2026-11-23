#include "ingest.hpp"

#include <fmt/format.h>
#include <fmt/std.h>
#include <htslib/faidx.h>
#include <htslib/hts.h>
#include <htslib/sam.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "apb_db/apb_db_core.hpp"
#include "apb_db/apb_db_schema.hpp"
#include "apb_db/apb_db_transform_hts.hpp"
#include "generic/describe.hpp"
#include "utility/apb_assert.hpp"
#include "utility/cleanup.hpp"


struct pileup_capture_t {
  htsFile* br_fh = nullptr;
  hts_itr_t* o_it = nullptr;  // owned; freed by PileupIterator::destroy
};
extern "C" {
int pileup_func (void* br_data, bam1_t* br_b)
{
  const pileup_capture_t* br_d = (pileup_capture_t*)(br_data);
  // No filtering
  return sam_itr_next (br_d->br_fh, br_d->o_it, br_b);
}
}
struct pileup_iterator_t {
  pileup_capture_t* o_cap = nullptr;
  bam_plp_t o_plp = nullptr;
  const bam_pileup1_t* br_plpArr = nullptr;
  size_t nPlp = 0;

  void destroy() noexcept
  {
    if (o_cap != nullptr) {
      hts_itr_destroy (o_cap->o_it);
      delete o_cap;
    }
    if (o_plp != nullptr) {
      bam_plp_destroy (o_plp);
    }
    br_plpArr = nullptr;
  }
};
struct aln_file_t {
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
struct locus_t {
  std::string contig;
  hts_pos_t pos_0ho;
};

static std::expected<locus_t, std::string> parse_locus (
    const std::string& locus, bool zeroBasedInputLocus
)
{
  locus_t out;
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
      out.pos_0ho = std::stoll (locus.substr (locusStrSepPos + 1));
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
    out.contig = locus.substr (1, locusStrSepPos - 2);
  }
  else {
    out.contig = locus.substr (0, locusStrSepPos);
  }

  if (!zeroBasedInputLocus) {
    // shift result to zero based
    if (out.pos_0ho == 0) {
      return std::unexpected (
          "Invalid locus specifier for one-based input; position is less "
          "than 1"
      );
    }
    out.pos_0ho--;
  }

  return out;
}

// close a db from open_new_db, removing its file if it has one
static void discard_new_db (
    sqlite3* conn, const std::optional<std::filesystem::path>& dbOutPath
)
{
  // uncommitted transaction rolls back on close
  sqlite3_close_v2 (conn);
  if (dbOutPath) {
    std::error_code ec;  // best effort
    std::filesystem::remove (*dbOutPath, ec);
  }
}

// open a new apb with schema created
// inside an open transaction. Caller commits, or discard_new_db on
// failure.
static conn_or_failmsg_t open_new_db (
    const std::optional<std::filesystem::path>& dbOutPath
)
{
  if (std::error_code ec;
      dbOutPath && std::filesystem::exists (
                       std::filesystem::symlink_status (*dbOutPath, ec)
                   )) {
    return std::unexpected (
        fmt::format (
            "{} already exists; refusing to overwrite", *dbOutPath
        )
    );
  }
  // "" is an sqlite private temp db
  const std::string name = dbOutPath ? dbOutPath->string() : "";

  sqlite3* conn = nullptr;
  Defer conn_cleanup ([&]() {
    discard_new_db (conn, dbOutPath);
  });  // cancel on success

  int rc = sqlite3_open_v2 (
      name.c_str(), &conn, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL
  );
  if (rc == SQLITE_OK) {
    // fk pragma is a no-op inside a transaction
    rc = sqlite3_exec (conn, schema::sqlSetFK.data(), NULL, NULL, NULL);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_exec (conn, "BEGIN;", NULL, NULL, NULL);
  }
  if (rc == SQLITE_OK) {
    // in transaction; failed ingest leaves no schema behind
    rc = apb_create_schema (conn);
  }

  if (rc == SQLITE_OK) {
    conn_cleanup.cancel();
    return conn;
  }
  if (rc == SQLITE_NOMEM) {
    // FIXME: nomem might be worth failing gracefully on elsewhere also,
    // since running out of memory on a cluster interactive session
    // or head node is not implausible.
    return std::unexpected (
        "Failed to initialise database before "
        "attempting to load pileup with out of memory error. "
        "Are you in a memory constrained environment?"
    );
  }
  if (dbOutPath) {
    return std::unexpected (
        fmt::format (
            "Failed to create database at {}, reporting: {} - {}",
            *dbOutPath, sqlite3_errstr (rc), sqlite3_errmsg (conn)
        )
    );
  }
  return std::unexpected (
      fmt::format (
          "Failed to create temporary database file (is temp area"
          "writable?), reporting: {} - {}",
          sqlite3_errstr (rc), sqlite3_errmsg (conn)
      )
  );
}

// 0-based half-open span of inserted records, for fetching the
// reference slice. Converts from 1-based closed db coordinates.
// Callers must have inserted records, all with an end.
// FIXME: add tests
static std::expected<std::pair<int64_t, int64_t>, int> records_span_0ho (
    sqlite3* conn
)
{
  sqlite3_stmt* o_stmt = nullptr;
  if (const auto rc = sqlite3_prepare_v2 (
          conn, "SELECT MIN(start), MAX(end) FROM sam_records;", -1,
          &o_stmt, NULL
      );
      rc != SQLITE_OK) {
    return std::unexpected (rc);
  }
  Defer stmt_cleanup ([&o_stmt]() { sqlite3_finalize (o_stmt); });
  if (const auto rc = sqlite3_step (o_stmt); rc != SQLITE_ROW) {
    return std::unexpected (rc);
  }
  APB_ASSERT (
      sqlite3_column_type (o_stmt, 0) != SQLITE_NULL &&
      sqlite3_column_type (o_stmt, 1) != SQLITE_NULL
  );
  // 1-based closed end == 0-based half-open end
  return std::pair{
      sqlite3_column_int64 (o_stmt, 0) - 1,
      sqlite3_column_int64 (o_stmt, 1)
  };
}

conn_or_failmsg_t ingest_sam (
    const std::optional<std::filesystem::path>& dbOutPath,
    const std::string& locus, bool zeroBasedLocus,
    const std::vector<std::filesystem::path>& alnPaths,
    const std::optional<std::filesystem::path>& refPath,
    std::optional<size_t> loadLimitBytes,
    std::optional<size_t> loadLimitDepth
)
{
  locus_t parsedLocus;
  if (const auto ret = parse_locus (locus, zeroBasedLocus); !ret) {
    return std::unexpected (ret.error());
  }
  else {
    parsedLocus = *ret;
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

  sqlite3* conn = nullptr;
  if (const auto ret = open_new_db (dbOutPath); !ret) {
    return std::unexpected (ret.error());
  }
  else {
    conn = *ret;
  }
  Defer conn_cleanup ([&]() {
    discard_new_db (conn, dbOutPath);
  });  // cancel on success

  // iterate alignment paths. Open, validate, pileup,
  // transform to db
  bool contigFound = false;
  bool readsFoundAtLocus = false;
  uint64_t estLocusBytes = 0;
  std::array<apb_row1_t, 5000> inputRecBuf;
  for (const auto& fp : alnPaths) {
    aln_file_t aln;
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
                sam_hdr_name2tid (aln.o_hdr, parsedLocus.contig.c_str());
            rc) {
    case -2:
      return std::unexpected (
          fmt::format (
              "Error while attempting to retrive contig id for contig "
              "{} from alignment file {}",
              parsedLocus.contig, aln.path
          )
      );
    case -1:
      break;
    default:
      alnHasContig = true;
      contigFound = true;
      aln.locusTid = static_cast<uint32_t> (rc);
    };

    pileup_iterator_t pileup;
    Defer pileup_destroy ([&pileup]() { pileup.destroy(); });
    if (alnHasContig) {
      /*
        Creates new hts_itr_t each call since in this program there is
        at present only a single call to this function per program instance,
        and even in future there is no expected pattern to loci at which pileups
        might be needed, hence little advantage to keeping the iterator alive between calls.
      */

      auto* o_alnIter = sam_itr_queryi (
          aln.o_idx, static_cast<int> (aln.locusTid), parsedLocus.pos_0ho,
          parsedLocus.pos_0ho + 1
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

      pileup.o_cap = new pileup_capture_t{aln.o_fh, o_alnIter};
      pileup.o_plp = bam_plp_init (pileup_func, pileup.o_cap);
      if (pileup.o_plp == NULL) {
        return std::unexpected (
            fmt::format (
                "Failed to create pileup iterator for alignment at {}",
                aln.path
            )
        );
      }
      // +1 so exceeding the limit is detectable below.
      // htslib takes int; anything above INT_MAX is effectively uncapped
      const int maxCnt = loadLimitDepth
                             ? static_cast<int> (std::min<size_t> (
                                   *loadLimitDepth, INT_MAX - 1
                               )) + 1
                             : INT_MAX;
      bam_plp_set_maxcnt (pileup.o_plp, maxCnt);

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
        if (plpPos < parsedLocus.pos_0ho) {
          continue;  // yet to reach locus
        }
        if (plpPos > parsedLocus.pos_0ho) {
          break;
        }
        // else locus found (implying reads present)
        pileup.br_plpArr = br_plpArr;
        pileup.nPlp = static_cast<size_t> (nPlp);
        if (loadLimitDepth && pileup.nPlp > *loadLimitDepth) {
          return std::unexpected (
              fmt::format (
                  "Pileup at locus exceeds {} reads in alignment file {}, "
                  "aborting. To uncap memory usage, use --no-limit-mem",
                  *loadLimitDepth, aln.path
              )
          );
        }
        for (int j = 0; j < nPlp; j++) {
          const auto* b1 = br_plpArr[j].b;
          if (loadLimitBytes) {
            estLocusBytes += sizeof (bam_pileup1_t) + sizeof (bam1_t) +
                             static_cast<unsigned long> (b1->l_data);
            if (estLocusBytes > *loadLimitBytes) {
              return std::unexpected (
                  fmt::format (
                      "Pileup at locus exceeds ~{}Gb in size, "
                      "aborting. To uncap memory usage, use "
                      "--no-limit-mem",
                      *loadLimitBytes / 1'000'000'000
                  )
              );
            }
          }
        }
        break;
      }
      // tid/pos are untouched if no column was ever yielded; only nPlp signals error
      if (nPlp < 0) {
        // FIXME: Again, what is the error space here? should this be a crash or a skip. read src.
        return std::unexpected (
            fmt::format (
                "Unexpected error encountered when iterating pileup for "
                "alignment at {}",
                aln.path
            )
        );
      }
    }

    int alnID;
    if (const auto ret =
            apb_insert_aln (conn, aln.path.string(), alnHasContig);
        !ret) {
      return std::unexpected (apb_describe_sqlite_failure (
          "Failed to insert alignment file", ret.error(),
          sqlite3_errmsg (conn)
      ));
    }
    else {
      alnID = *ret;
    }
    if (!alnHasContig || pileup.nPlp < 1) {
      continue;
    }
    readsFoundAtLocus = true;


    const auto* pEnd = pileup.br_plpArr + pileup.nPlp;
    size_t p1Consumed = 0;
    size_t nBufValid = 0;
    // consume records in chunks
    while (p1Consumed < pileup.nPlp) {
      if (const auto ret = apb_transform_pileup1 (
              {pileup.br_plpArr + p1Consumed, pEnd}, inputRecBuf,
              [&aln] (int mtid) {
                return sam_hdr_tid2name (aln.o_hdr, mtid);
              }
          );
          !ret) {
        const auto& err = ret.error();
        switch (err.code) {
        case transform_err_t::aux_parse_fail:
          return std::unexpected (
              fmt::format (
                  "Unspecified failure in htslib when attempting to parse "
                  "aux tag in read {}. Is tag data corrupt?",
                  bam_get_qname (err.b)
              )
          );
        }
      }
      else {
        p1Consumed += *ret;
        nBufValid = *ret;
      }
      if (const auto rc = apb_insert_records (
              conn, {begin (inputRecBuf), begin (inputRecBuf) + nBufValid},
              alnID
          );
          rc != SQLITE_OK) {
        return std::unexpected (apb_describe_sqlite_failure (
            "Failed to insert alignment data", rc, sqlite3_errmsg (conn)
        ));
      }
    }
  }

  if (!contigFound) {
    return std::unexpected (
        "Contig not found in any input alignment file"
    );
  }

  if (readsFoundAtLocus) {
    // Fetch reference for the records span and insert metadata
    std::optional<std::string> refSlice;
    if (ff) {
      // reads were found, and transformed reads always have an end
      const auto span = records_span_0ho (conn);
      if (!span) {
        return std::unexpected (apb_describe_sqlite_failure (
            "Failed to read records span", span.error(),
            sqlite3_errmsg (conn)
        ));
      }
      const auto [spanStart_0ho, spanEnd_0ho] = *span;
      hts_pos_t regLen;
      // uses closed coordinates, hence -1
      auto* o_fetch = faidx_fetch_seq64 (
          *ff, parsedLocus.contig.c_str(), spanStart_0ho, spanEnd_0ho - 1,
          &regLen
      );
      if (o_fetch == NULL) {
        return std::unexpected (
            fmt::format (
                "Failed to fetch reference region from fasta at {} "
                "for span {}:{}-{}",
                refPath.value(), parsedLocus.contig, spanStart_0ho,
                spanEnd_0ho
            )
        );
      }
      if (regLen < (spanEnd_0ho - spanStart_0ho)) {
        return std::unexpected (
            "Reference too short! Fasta missing coverage for pileup span."
        );
      }
      refSlice = {o_fetch, static_cast<size_t> (regLen)};
      free (o_fetch);
    }

    if (const auto rc = apb_insert_locus (
            conn, parsedLocus.contig, parsedLocus.pos_0ho, refSlice
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

  if (const auto rc = sqlite3_exec (conn, "COMMIT;", NULL, NULL, NULL);
      rc != SQLITE_OK) {
    return std::unexpected (
        fmt::format (
            "Failed to commit database transaction, reporting: {} - {}",
            sqlite3_errstr (rc), sqlite3_errmsg (conn)
        )
    );
  }

  conn_cleanup.cancel();
  return conn;
}

// percent-encode a path for use in an sqlite URI filename
static std::string uri_encode_path (const std::filesystem::path& path)
{
  static constexpr std::string_view kHex = "0123456789ABCDEF";
  std::string out;
  for (const char ch : path.string()) {
    const auto c = static_cast<unsigned char> (ch);
    if ((std::isalnum (c) != 0) || c == '/' || c == '-' || c == '.' ||
        c == '_' || c == '~') {
      out += static_cast<char> (c);
    }
    else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0xF];
    }
  }
  return out;
}

conn_or_failmsg_t ingest_db (const std::filesystem::path& dbInputPath)
{
  sqlite3* conn = nullptr;
  Defer conn_cleanup ([&]() { sqlite3_close_v2 (conn); });
  // immutable: apb never writes to DB, so skip locking, which is
  // unreliable on network filesystems
  const auto uri =
      "file:" + uri_encode_path (dbInputPath) + "?immutable=1";
  if (const auto rc = sqlite3_open_v2 (
          uri.c_str(), &conn, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, NULL
      );
      rc != SQLITE_OK) {
    return std::unexpected (
        fmt::format (
            "sqlite3 failure when opening database at {}, reporting: "
            "{} ({}) - {}",
            dbInputPath, sqlite3_errstr (rc), rc, sqlite3_errmsg (conn)
        )
    );
  }

  // per connection; apb_verify_schema requires it
  if (const auto rc =
          sqlite3_exec (conn, schema::sqlSetFK.data(), NULL, NULL, NULL);
      rc != SQLITE_OK) {
    return std::unexpected (
        fmt::format (
            "sqlite3 failure when setting foreign key pragma on "
            "database at {}, reporting: {} ({}) - {}",
            dbInputPath, sqlite3_errstr (rc), rc, sqlite3_errmsg (conn)
        )
    );
  }

  /* validate */
  if (const auto ret = apb_verify_schema (conn); !ret) {
    return std::unexpected (
        fmt::format (
            "sqlite3 failure during schema verfication, reporting: {} - "
            "{}",
            sqlite3_errstr (ret.error()), sqlite3_errmsg (conn)
        )
    );
  }
  else {
    const auto& status = *ret;
    switch (status.code) {
    case apb_verify_result_t::success:
      break;
    case apb_verify_result_t::versionUnset:
      return std::unexpected (
          fmt::format (
              "db at {} is not an apb database (user_version not set)",
              dbInputPath
          )
      );
    case apb_verify_result_t::verMismatch:
      return std::unexpected (
          fmt::format (
              "db at {} is using an outdated or unknown "
              "version of the apb schema and cannot be read by this "
              "version of apb (expected schema version {}, got {}).",
              dbInputPath, APB_SCHEMA_VER, status.version
          )
      );
    case apb_verify_result_t::schemaMismatch:
      return std::unexpected (
          fmt::format (
              "db at {} does not have expected apb schema", dbInputPath
          )
      );
    case apb_verify_result_t::fkNotEnabled:
      APB_UNREACHABLE ("fk pragma not set on db connection before verify");
    }
  }
  if (const auto ret = apb_validate_db (conn); !ret) {
    return std::unexpected (
        fmt::format (
            "sqlite3 failure during database validation, reporting: {} - "
            "{}",
            sqlite3_errstr (ret.error()), sqlite3_errmsg (conn)
        )
    );
  }
  else {
    const auto& status = *ret;
    switch (status.code) {
    case apb_validate_result_t::success:
      break;
    case apb_validate_result_t::failedIntegrityCheck:
      return std::unexpected (
          fmt::format (
              "db at {} failed integrity check and is "
              "therefore not an operable apb database. The "
              "problems were:\n{}",
              dbInputPath, status.problems.value()
          )
      );
    case apb_validate_result_t::failedForeignKeyCheck:
      return std::unexpected (
          fmt::format (
              "db at {} failed foreign key check and is "
              "therefore not an operable apb database: The "
              "problems were:\n{}",
              dbInputPath, status.problems.value()
          )
      );
    case apb_validate_result_t::missingLocus:
      return std::unexpected (
          fmt::format (
              "db at {} has no locus metadata and is therefore not an "
              "operable apb database",
              dbInputPath
          )
      );
    case apb_validate_result_t::metadataMismatch:
      return std::unexpected (
          fmt::format (
              "db at {} has metadata inconsistent with its records and "
              "is therefore not an operable apb database. The problems "
              "were:\n{}",
              dbInputPath, status.problems.value()
          )
      );
    }
  }

  conn_cleanup.cancel();
  return conn;
}

/* demo */
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
  std::uniform_int_distribution<int> pick (0, 3);
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

conn_or_failmsg_t ingest_demo (
    const std::optional<std::filesystem::path>& dbOutPath
)
{
  sqlite3* conn = nullptr;
  if (const auto ret = open_new_db (dbOutPath); !ret) {
    return std::unexpected (ret.error());
  }
  else {
    conn = *ret;
  }
  Defer conn_cleanup ([&]() {
    discard_new_db (conn, dbOutPath);
  });  // cancel on success

  constexpr std::string_view demoContig = "demo-contig";
  constexpr int64_t pos = 10'000'000;
  constexpr size_t regWidth = 300;
  constexpr size_t nAln = 3;
  constexpr size_t nQueryPerAln = 300;

  // every read has a core aligned run of this length, covering the
  // pileup column.
  constexpr size_t readLen = (regWidth / 2) - 1;
  auto refSeq = fixed_ref_seq (regWidth);
  // refSeq[0] sits at this absolute position; pos is refSeq[readLen]
  constexpr int64_t winStart = pos - static_cast<int64_t> (readLen);
  // known ref base at pos
  refSeq[readLen] = 'A';

  constexpr size_t maxDelLen = 4;

  // start >= 2 leaves a base after the pileup column for an indel to follow.
  // headroom of maxDelLen keeps start+readLen+delLen within regWidth.
  std::uniform_int_distribution<size_t> gstartGen (2, readLen - maxDelLen);
  constexpr double mismatchRate = 0.01;
  std::bernoulli_distribution mismatchDist (mismatchRate);
  std::uniform_int_distribution<size_t> delLenGen (1, maxDelLen);
  constexpr size_t maxInsLen = 4;
  std::uniform_int_distribution<size_t> insLenGen (1, maxInsLen);
  constexpr size_t maxClipLen = 20;
  std::uniform_int_distribution<size_t> clipLenGen (1, maxClipLen);
  // mostly 60, some 0, a spread between
  std::discrete_distribution<int> mapQBand ({5, 15, 80});
  std::uniform_int_distribution<int> mapQMid (1, 59);
  const std::array<uint8_t, 5> qualVals{
      'F' - 33, 'E' - 33, 'D' - 33, ':' - 33, ';' - 33
  };
  std::discrete_distribution<int> qualValPicker ({80, 30, 20, 15, 15});

  // Each read carries one allele at the pileup column...
  // FIns/FDel follow the column; Del covers it.
  enum class Allele : uint8_t { Ref, Alt, FIns, FDel, Del };
  constexpr char pileupAlt = 'T';
  std::discrete_distribution<int> allelePicker (
      {0.45, 0.20, 0.12, 0.12, 0.11}
  );

  // and, independently, at most one soft clip.
  enum class AlignmentFeature : uint8_t { None, LeadClip, TailClip };
  std::discrete_distribution<int> featurePicker ({0.80, 0.10, 0.10});

  // All reads paired, mates on the same contig unless unmapped.
  std::bernoulli_distribution coinFlip (0.5);
  std::bernoulli_distribution mateUnmappedDist (0.05);
  std::uniform_int_distribution<int64_t> mateDistGen (100, 400);
  // at most one of none/dup/secondary/supplementary/qcfail
  constexpr std::array<uint16_t, 5> filterFlags{
      0, BAM_FDUP, BAM_FSECONDARY, BAM_FSUPPLEMENTARY, BAM_FQCFAIL
  };
  std::discrete_distribution<int> filterPicker ({87, 8, 2, 2, 1});

  // read/ref coordinates are absolute (0-based); index refSeq via winStart.
  // every read covers pos.
  std::mt19937 rng;
  // one record set per alignment file
  std::array<std::vector<apb_row1_t>, nAln> alns;
  for (size_t i = 0; i < nAln * nQueryPerAln; ++i) {
    const size_t alnIdx = i / nQueryPerAln;
    auto& rec = alns[alnIdx].emplace_back();
    auto& plp = rec.plp.emplace();

    rec.start_0ho = winStart + static_cast<int64_t> (gstartGen (rng));
    // offset of the pileup column within the core aligned run
    const auto colOff = static_cast<size_t> (pos - rec.start_0ho);

    const auto pushOp = [&rec] (size_t len, int op) {
      const auto cigel = static_cast<uint32_t> (bam_cigar_gen (len, op));
      rec.cigBlob.push_back (cigel);
    };
    const auto allele = static_cast<Allele> (allelePicker (rng));
    const auto feature =
        static_cast<AlignmentFeature> (featurePicker (rng));

    if (feature == AlignmentFeature::LeadClip) {
      pushOp (clipLenGen (rng), BAM_CSOFT_CLIP);
    }
    plp.indel = 0;  // meaning per htslib p1.indel
    if (allele == Allele::Del) {
      // column falls inside the deletion; >= 1 base precedes it
      const size_t n = delLenGen (rng);
      const size_t pre =
          colOff - std::uniform_int_distribution<size_t> (0, n - 1) (rng);
      pushOp (pre, BAM_CMATCH);
      pushOp (n, BAM_CDEL);
      pushOp (readLen - pre, BAM_CMATCH);
    }
    else if (allele == Allele::FIns || allele == Allele::FDel) {
      // indel immediately after the pileup column
      const bool isDel = allele == Allele::FDel;
      const size_t n = isDel ? delLenGen (rng) : insLenGen (rng);
      pushOp (colOff + 1, BAM_CMATCH);
      pushOp (n, isDel ? BAM_CDEL : BAM_CINS);
      pushOp (readLen - colOff - 1, BAM_CMATCH);
      plp.indel = isDel ? -static_cast<int> (n) : static_cast<int> (n);
    }
    else {
      pushOp (readLen, BAM_CMATCH);
    }
    if (feature == AlignmentFeature::TailClip) {
      pushOp (clipLenGen (rng), BAM_CSOFT_CLIP);
    }

    // Walk the CIGAR to build the read sequence, tracking ref and query
    // cursors. The pileup column is inside an M op, or a D op for Del.
    std::string seq;
    int64_t nm = 0;  // edit distance, for the NM tag
    auto refPos = rec.start_0ho;
    for (const auto cig : rec.cigBlob) {
      const size_t len = bam_cigar_oplen (cig);
      switch (bam_cigar_op (cig)) {
      case BAM_CMATCH:
        for (size_t k = 0; k < len; ++k, ++refPos) {
          const char refBase =
              refSeq[static_cast<size_t> (refPos - winStart)];
          char b;
          if (refPos == pos) {
            plp.qPos_0ho = static_cast<int32_t> (seq.size());
            b = allele == Allele::Alt ? pileupAlt : refBase;
          }
          else {
            b = mismatchDist (rng) ? mutate_base (refBase, rng) : refBase;
          }
          if (b != refBase) {
            ++nm;
          }
          seq += b;
        }
        break;
      case BAM_CDEL:
        if (pos >= refPos && pos < refPos + static_cast<int64_t> (len)) {
          // htslib: qpos of a deleted column is the next query base
          plp.qPos_0ho = static_cast<int32_t> (seq.size());
          plp.is_del = true;
        }
        refPos += static_cast<int64_t> (len);
        nm += static_cast<int64_t> (len);
        break;
      case BAM_CINS:
        nm += static_cast<int64_t> (len);
        [[fallthrough]];
      default:  // insertion, soft clip: bases not in the reference
        for (size_t k = 0; k < len; ++k) {
          seq += random_base (rng);
        }
        break;
      }
    }
    APB_ASSERT (plp.qPos_0ho >= 0);
    rec.end_0ho = refPos;

    std::string qual (seq.size(), ' ');
    for (auto& q : qual) {
      q = static_cast<char> (
          qualVals[static_cast<size_t> (qualValPicker (rng))] + 33
      );
    }

    plp.is_refskip = false;  // not in demo
    rec.seq = std::move (seq);
    rec.qual = std::move (qual);

    uint16_t flag = BAM_FPAIRED;
    flag |= coinFlip (rng) ? BAM_FREAD1 : BAM_FREAD2;
    flag |= filterFlags[static_cast<size_t> (filterPicker (rng))];
    const bool isRev = coinFlip (rng);
    if (isRev) {
      flag |= BAM_FREVERSE;
    }
    // unmapped mate sits at this read's position, per SAM recommended
    // practice (bwa, minimap2, fixmate, htsjdk)
    rec.mStart_0ho = rec.start_0ho;
    if (mateUnmappedDist (rng)) {
      flag |= BAM_FMUNMAP;
    }
    else {
      flag |= BAM_FPROPER_PAIR;
      // FR orientation: mate on the opposite strand, facing this read
      const auto d = mateDistGen (rng);
      rec.mStart_0ho = isRev ? rec.start_0ho - d : rec.start_0ho + d;
      if (!isRev) {
        flag |= BAM_FMREVERSE;
      }
    }
    rec.flag = flag;
    rec.mtidName = demoContig;  // single demo contig

    rec.mapQ = 60;
    switch (mapQBand (rng)) {
    case 0:
      rec.mapQ = 0;
      break;
    case 1:
      rec.mapQ = static_cast<uint8_t> (mapQMid (rng));
      break;
    default:
      break;
    }

    rec.qName = "read" + std::to_string (i);
    // one read group per alignment file
    rec.auxJson =
        fmt::format (R"({{"NM":{},"RG":"sample{}"}})", nm, alnIdx + 1);
  }

  for (size_t i = 0; i < alns.size(); ++i) {
    auto& aln = alns[i];
    std::sort (begin (aln), end (aln), [] (const auto& a, const auto& b) {
      return a.start_0ho < b.start_0ho;
    });
    std::string alnPath{"/data/samples/PD12345"};
    alnPath += static_cast<char> ('a' + i);
    alnPath.append (".bam");

    int alnId;
    // sqlite failures are reachable with a disk target (e.g. disk full)
    if (const auto ret = apb_insert_aln (conn, alnPath, true); !ret) {
      return std::unexpected (apb_describe_sqlite_failure (
          "Failed to insert demo alignment file", ret.error(),
          sqlite3_errmsg (conn)
      ));
    }
    else {
      alnId = *ret;
    }

    if (const auto rc = apb_insert_records (conn, aln, alnId);
        rc != SQLITE_OK) {
      return std::unexpected (apb_describe_sqlite_failure (
          "Failed to insert demo alignment data", rc, sqlite3_errmsg (conn)
      ));
    }
  }

  const auto span = records_span_0ho (conn);
  if (!span) {
    return std::unexpected (apb_describe_sqlite_failure (
        "Failed to read demo records span", span.error(),
        sqlite3_errmsg (conn)
    ));
  }
  const auto [spanStart_0ho, spanEnd_0ho] = *span;
  const auto ref = refSeq.substr (
      static_cast<size_t> (spanStart_0ho - winStart),
      static_cast<size_t> (spanEnd_0ho - spanStart_0ho)
  );
  if (const auto rc =
          apb_insert_locus (conn, std::string{demoContig}, pos, ref);
      rc != SQLITE_OK) {
    return std::unexpected (apb_describe_sqlite_failure (
        "Failed to insert demo locus metadata", rc, sqlite3_errmsg (conn)
    ));
  }
  if (const auto rc = sqlite3_exec (conn, "COMMIT;", NULL, NULL, NULL);
      rc != SQLITE_OK) {
    return std::unexpected (
        fmt::format (
            "Failed to commit database transaction, reporting: {} - {}",
            sqlite3_errstr (rc), sqlite3_errmsg (conn)
        )
    );
  }

  conn_cleanup.cancel();
  return conn;
}
