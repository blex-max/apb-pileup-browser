#pragma once

#include <htslib/sam.h>

#include <expected>
#include <format>
#include <functional>
#include <string_view>
#include <type_traits>

#include "backend/PileupDB.hpp"
#include "backend/hts_types.hpp"
#include "backend/sql.hpp"
#include "backend/sql_types.hpp"
#include "plog/Log.h"
#include "shared/err.hpp"

// All these types might be overkill
struct SqliteStmtStatic : public SqliteStmt {
  static inline const std::string_view rsql_stmt;
};

struct InsertMetadataStmt : public SqliteStmtStatic {
  static inline const std::string_view rsql_stmt =
      rsql_InsertMetadata;
};
struct InsertLociStmt : public SqliteStmtStatic {
  static inline const std::string_view rsql_stmt =
      rsql_InsertLoci;
};
struct InsertReadsStmt : public SqliteStmtStatic {
  static inline const std::string_view rsql_stmt =
      rsql_InsertReads;
};
using MetadataStmtOrErr = std::expected<InsertMetadataStmt, Err>;
using LociStmtOrErr = std::expected<InsertLociStmt, Err>;
using ReadsStmtOrErr = std::expected<InsertReadsStmt, Err>;

template <typename StmtT>
  requires std::is_base_of_v<SqliteStmtStatic, StmtT>
inline std::expected<StmtT, Err> prepare (PileupDB& db)
{
  StmtT stmt;
  int rc;
  if (rc = sqlite3_prepare_v2 (
          db, StmtT::rsql_stmt.data(),
          static_cast<int> (StmtT::rsql_stmt.size()),
          &stmt.o_ptr, NULL
      );
      rc != SQLITE_OK) {
    return std::unexpected{
        make_sqlite3_err (rc, sqlite3_errmsg (db))
    };
  }
  return stmt;
}

struct SqliteStmtDynamic : public SqliteStmt {
  static inline const std::string_view rsql_prefix;
};
struct DynamicSelectReadsStmt : public SqliteStmtDynamic {
  static inline const std::string_view rsql_prefix =
      "SELECT * FROM reads";
};
struct DynamicFragments {
  std::vector<std::string> where;
  std::string orderBy;
  size_t offset;
};
inline std::expected<DynamicSelectReadsStmt, Err>
prepare_select_reads (
    const PileupDB& db, const DynamicFragments& frags
)
{
  DynamicSelectReadsStmt stmt;

  std::string rsql_builtStmt{stmt.rsql_prefix};

  // build WHERE
  std::string fragWhere;
  for (size_t i = 0; i < frags.where.size(); ++i) {
    fragWhere.append (frags.where[i]);
    if (i != (frags.where.size() - 1)) {
      fragWhere.append (" ");
    }
  }

  if (!fragWhere.empty()) {
    rsql_builtStmt.append (" WHERE ");
    rsql_builtStmt.append (fragWhere);
  }

  if (!frags.orderBy.empty()) {
    rsql_builtStmt.append (" ORDER BY ");
    rsql_builtStmt.append (frags.orderBy);
  }

  // SQLite requires LIMIT whenever OFFSET is present; -1 means unbounded.
  if (frags.offset) {
    rsql_builtStmt.append (" LIMIT -1 OFFSET ");
    rsql_builtStmt.append (std::to_string (frags.offset));
  }

  rsql_builtStmt.append (";");  // end stmt

  PLOGD << "Compiling user query: " + rsql_builtStmt;

  // Either of the following cases should be surfaced to the user

  int rc;
  if (rc = sqlite3_prepare_v2 (
          db, rsql_builtStmt.c_str(),
          static_cast<int> (rsql_builtStmt.size()), &stmt.o_ptr,
          NULL
      );
      rc != SQLITE_OK) {
    return std::unexpected{make_sqlite3_err (
        rc, std::format (
                "Could not compile statement: {} - {}",
                rsql_builtStmt, sqlite3_errmsg (db)
            )
    )};
  }

  if (!sqlite3_stmt_readonly (stmt)) {
    // TODO: proper err
    return std::unexpected{
        make_internal_err ("Statement would modify database.")
    };
  }

  return stmt;
};

// for use as a buffer during conversion
struct PileupFields {
  // NOTE: layout as table schema
  std::string qName;
  uint16_t flag;
  hts_pos_t start;
  hts_pos_t end;
  uint8_t mapQ;

  char base;
  uint8_t baseQual;
  int32_t qPos;
  int indel;
  bool isDel, isHead, isTail, isRefSkip;

  std::string cig;
  std::string seqBases;
  std::string qualAscii;

  std::string mtidName;
  hts_pos_t mStart;

  std::string auxJson;

  std::vector<uint32_t> rawCig;
  size_t nCig;
};

/* htslib PILEUP MACHINERY */
struct PileupCapture {
  htsFile* uo_fh = nullptr;   // UnOwned
  hts_itr_t* o_it = nullptr;  // Owned
};
struct PreparedPileup {
  PileupCapture* o_cap =
      nullptr;  // I don't think it's actually necessary to retain this member
  // I think bam_plp_t is complete storage for the array
  bam_plp_t o_plp = nullptr;
  const bam_pileup1_t* plpArr = nullptr;
  size_t nPlp = 0;

  ~PreparedPileup()
  {
    if (o_cap) {
      hts_itr_destroy (o_cap->o_it);
      delete o_cap;
    }
    if (o_plp) {
      bam_plp_destroy (o_plp);
    }
    plpArr = nullptr;
  }
  PreparedPileup() = default;
  PreparedPileup (PreparedPileup&) = delete;
  PreparedPileup& operator= (PreparedPileup&) = delete;
  PreparedPileup (PreparedPileup&& o)
      : o_cap (o.o_cap),
        o_plp (o.o_plp),
        plpArr (o.plpArr),
        nPlp (o.nPlp)
  {
    o.o_cap = nullptr;
    o.o_plp = nullptr;
    o.plpArr = nullptr;
    o.nPlp = 0;
  };
  PreparedPileup& operator= (PreparedPileup&&) = delete;
};

using PileupOrErr = std::expected<PreparedPileup, Err>;
PileupOrErr prepare_pileup (
    const AlnFile& aln, const PileupPosition& pos
);

// resolves a bam1_t core.mtid/core.tid to a contig name, e.g. via sam_hdr_tid2name.
using Tid2StrFn = std::function<const char*(int)>;

// insert pileup loci into database, returning id.
[[nodiscard]] IntOrErr insert_loci (
    PileupDB& db, const PileupPosition& pos, Tid2StrFn tid2str
);

// Insert reads covering a pileup position into database.
// plpArr/nPlp: the pileup array produced by prepare_pileup, for the
// locus identified by lociId.
[[nodiscard]] VoidOrErr insert_reads_internal (
    PileupDB& db, const bam_pileup1_t* plpArr, size_t nPlp,
    int lociId, Tid2StrFn tid2str
);

// Bind one pileup row's fields into `stmt`, in column order matching
// stmt_str_InsertReads. lociId identifies the locus this read belongs
// to. Returns the sqlite3 result code of the first failing bind call,
// or SQLITE_OK if all columns bound successfully.
[[nodiscard]] int bind_pileup_fields (
    InsertReadsStmt& stmt, sqlite3_int64 lociId,
    const PileupFields& pf
);

// convert to database-facing interface type
// NOTE: noexcept?
VoidOrErr fill_fields (
    PileupFields& pf, const bam_pileup1_t* p1,
    const char* mTidName
);
