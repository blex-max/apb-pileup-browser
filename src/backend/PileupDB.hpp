#pragma once

#include <fmt/format.h>
#include <htslib/sam.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "backend/hts_types.hpp"
#include "backend/sql_types.hpp"
#include "plog/Log.h"
#include "shared/err.hpp"

struct PileupDB : public SqliteConn {};
VoidOrErr init_db (PileupDB& db);

// insert provenance metadata into database (unlinked to loci/reads —
// one alignment file per db).
[[nodiscard]] VoidOrErr insert_metadata (
    PileupDB& db, const AlnFile& aln
);

// insert reads at pileup position into database
[[nodiscard]] VoidOrErr insert_pileup (
    PileupDB& db, const AlnFile& aln, const PileupPosition& pos,
    const std::optional<FastaFile>& ff
);

// Copy the in-memory database out to a file on disk, using
// sqlite3's online backup API.
[[nodiscard]] VoidOrErr dump_to_disk (
    const PileupDB& db, const std::string& path
);

// Copy a database file on disk into an in-memory PileupDB,
// using sqlite3's online backup API.
[[nodiscard]] VoidOrErr load_from_disk (
    PileupDB& db, const std::string& path
);

// Plain {contig, pos} pair read back from the loci table. Distinct from
// PileupPosition (backend/hts_types.hpp), which stores a numeric htslib
// tid rather than a resolved contig name -- that type is only meaningful
// while an AlnFile/header is open (AlnModeArgs ingest path); this one is
// read directly out of sqlite, with no htslib dependency, so it works
// equally when the db was loaded from disk with no header in scope.
struct LocusData {
  std::string contig;
  int64_t pos;  // 0-based pileup position, per loci.pos
  int64_t start;
  int64_t end;
  std::optional<std::string> refSlice;
};
using LocusOrErr = std::expected<LocusData, Err>;

// The single point where a resolved contig name + pileup span becomes a
// persisted LocusData. Callers must resolve the contig name themselves
// (e.g. via sam_hdr_tid2name) -- PileupPosition's tid is only meaningful
// while the originating AlnFile/header is open.
LocusData make_locus_data (
    std::string contigName, hts_pos_t pos,
    const GenomicSpan& span, std::optional<std::string> refSlice
);

// Read back the single row of the loci table. Every PileupDB has exactly
// one locus for its lifetime (one pileup position per invocation, written
// once by insert_loci) -- this assumes that invariant rather than
// defensively checking for zero or multiple rows.
[[nodiscard]] LocusOrErr get_locus_data (const PileupDB& db);

// TIED TO SCHEMA CREATE ORDER
enum SelectFields {
  id,  // 0
  loci_id,
  qname,
  flag,
  rstart,
  rend,
  mapq,
  base,
  basequal,
  qpos,
  indel,
  is_del,
  is_head,
  is_tail,
  is_refskip,
  cigar,
  seq,
  qual,
  mtid,
  mstart,
  tags,
  cig_uint32,
  ncig
};

inline int64_t get_id (sqlite3_stmt* row)
{
  return sqlite3_column_int64 (row, SelectFields::id);
}
inline int64_t get_loci_id (sqlite3_stmt* row)
{
  return sqlite3_column_int64 (row, SelectFields::loci_id);
}
inline std::string get_qname (sqlite3_stmt* row)
{
  if (sqlite3_column_type (row, SelectFields::qname) ==
      SQLITE_NULL) {
    return "";
  }
  const auto* p = sqlite3_column_text (row, SelectFields::qname);
  const auto len =
      sqlite3_column_bytes (row, SelectFields::qname);
  return std::string (
      reinterpret_cast<const char*> (p),
      static_cast<size_t> (len)
  );
}
inline uint16_t get_flag (sqlite3_stmt* row)
{
  return static_cast<uint16_t> (
      sqlite3_column_int (row, SelectFields::flag)
  );
}
inline int64_t get_rstart (sqlite3_stmt* row)
{
  return sqlite3_column_int64 (row, SelectFields::rstart);
}
inline int64_t get_rend (sqlite3_stmt* row)
{
  return sqlite3_column_int64 (row, SelectFields::rend);
}
inline uint8_t get_mapq (sqlite3_stmt* row)
{
  return static_cast<uint8_t> (
      sqlite3_column_int (row, SelectFields::mapq)
  );
}
inline char get_base (sqlite3_stmt* row)
{
  const auto* p = sqlite3_column_text (row, SelectFields::base);
  return static_cast<char> (p[0]);
}
inline uint8_t get_basequal (sqlite3_stmt* row)
{
  return static_cast<uint8_t> (
      sqlite3_column_int (row, SelectFields::basequal)
  );
}
inline uint64_t get_qpos (sqlite3_stmt* row)
{
  return static_cast<uint64_t> (
      sqlite3_column_int (row, SelectFields::qpos)
  );
}
inline int64_t get_indel (sqlite3_stmt* row)
{
  return static_cast<int64_t> (
      sqlite3_column_int (row, SelectFields::indel)
  );
}
inline bool get_is_del (sqlite3_stmt* row)
{
  return sqlite3_column_int (row, SelectFields::is_del) != 0;
}
inline bool get_is_head (sqlite3_stmt* row)
{
  return sqlite3_column_int (row, SelectFields::is_head) != 0;
}
inline bool get_is_tail (sqlite3_stmt* row)
{
  return sqlite3_column_int (row, SelectFields::is_tail) != 0;
}
inline bool get_is_refskip (sqlite3_stmt* row)
{
  return sqlite3_column_int (row, SelectFields::is_refskip) != 0;
}
inline std::string get_cigar (sqlite3_stmt* row)
{
  const auto* p = sqlite3_column_text (row, SelectFields::cigar);
  const auto len =
      sqlite3_column_bytes (row, SelectFields::cigar);
  return std::string (
      reinterpret_cast<const char*> (p),
      static_cast<size_t> (len)
  );
}
inline std::string get_seq (sqlite3_stmt* row)
{
  const auto* p = sqlite3_column_text (row, SelectFields::seq);
  const auto len = sqlite3_column_bytes (row, SelectFields::seq);
  return std::string (
      reinterpret_cast<const char*> (p),
      static_cast<size_t> (len)
  );
}
inline std::string get_qual (sqlite3_stmt* row)
{
  const auto* p = sqlite3_column_text (row, SelectFields::qual);
  const auto len =
      sqlite3_column_bytes (row, SelectFields::qual);
  return std::string (
      reinterpret_cast<const char*> (p),
      static_cast<size_t> (len)
  );
}
inline std::string get_mtid (sqlite3_stmt* row)
{
  if (sqlite3_column_type (row, SelectFields::mtid) ==
      SQLITE_NULL) {
    return "";
  }
  const auto* p = sqlite3_column_text (row, SelectFields::mtid);
  const auto len =
      sqlite3_column_bytes (row, SelectFields::mtid);
  return std::string (
      reinterpret_cast<const char*> (p),
      static_cast<size_t> (len)
  );
}
// NULL (no mate/next-read reference) round-trips to -1, mirroring
// bind_pileup_fields' pf.mStart < 0 => NULL convention on the way in.
inline int64_t get_mstart (sqlite3_stmt* row)
{
  if (sqlite3_column_type (row, SelectFields::mstart) ==
      SQLITE_NULL) {
    return -1;
  }
  return sqlite3_column_int64 (row, SelectFields::mstart);
}
inline std::string get_tags (sqlite3_stmt* row)
{
  if (sqlite3_column_type (row, SelectFields::tags) ==
      SQLITE_NULL) {
    return "";
  }
  const auto* p = sqlite3_column_text (row, SelectFields::tags);
  const auto len =
      sqlite3_column_bytes (row, SelectFields::tags);
  return std::string (
      reinterpret_cast<const char*> (p),
      static_cast<size_t> (len)
  );
}
// Unlike the text accessors above, this returns a pointer straight into the
// statement's row buffer rather than an owned copy -- valid only until the
// next type-conversion call on this column, or a step/reset/finalize.
// Deliberate: cig_uint32 is only ever needed transiently, in the same
// row-processing scope it's fetched in.
inline const uint32_t* get_cigar_blob (sqlite3_stmt* row)
{
  return static_cast<const uint32_t*> (
      sqlite3_column_blob (row, SelectFields::cig_uint32)
  );
}
inline uint64_t get_ncig (sqlite3_stmt* row)
{
  return static_cast<uint64_t> (
      sqlite3_column_int (row, SelectFields::ncig)
  );
}

// Steps `stmt` forward by one row. Returns true if a row is now available
// -- extract whatever columns you need via the get_* accessors above --
// or false once the result set is exhausted. Has no opinion on what was
// selected or in what order; it is not specific to the reads table's
// column layout.
[[nodiscard]] BoolOrErr next_read (
    sqlite3_stmt* stmt, const PileupDB& db
);

struct DynamicSelectReadsStmt : public SqliteStmt {
  static inline const std::string_view rsql_prefix =
      "SELECT * FROM reads";
};
struct DynamicFragments {
  std::vector<std::string> where;
  std::string orderBy;
};

using SelectStmtOrErr =
    std::expected<DynamicSelectReadsStmt, Err>;
inline SelectStmtOrErr prepare_select_reads (
    const PileupDB& db, const DynamicFragments& frags
)
{
  DynamicSelectReadsStmt stmt;

  std::string rsql_builtStmt{stmt.rsql_prefix};

  // build WHERE
  if (!frags.where.empty()) {
    rsql_builtStmt.append (" WHERE ");
    for (size_t i = 0; i < frags.where.size(); ++i) {
      rsql_builtStmt.append (frags.where[i]);
      if (i != (frags.where.size() - 1)) {
        rsql_builtStmt.append (" ");
      }
    }
  }

  if (!frags.orderBy.empty()) {
    rsql_builtStmt.append (" ORDER BY ");
    rsql_builtStmt.append (frags.orderBy);
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
        rc, fmt::format (
                "Could not compile statement: {} - {}",
                rsql_builtStmt, sqlite3_errmsg (db)
            )
    )};
  }

  if (!sqlite3_stmt_readonly (stmt)) {
    return std::unexpected{
        make_internal_err ("Statement would modify database.")
    };
  }

  return stmt;
};

struct DynamicCountReadsStmt : public SqliteStmt {
  static inline const std::string_view rsql_prefix =
      "SELECT COUNT(*) FROM reads";
};

using CountStmtOrErr = std::expected<DynamicCountReadsStmt, Err>;
inline CountStmtOrErr prepare_count_reads (
    const PileupDB& db, const std::vector<std::string>& where
)
{
  DynamicCountReadsStmt stmt;

  std::string rsql_builtStmt{stmt.rsql_prefix};

  // build WHERE
  if (!where.empty()) {
    rsql_builtStmt.append (" WHERE ");
    for (size_t i = 0; i < where.size(); ++i) {
      rsql_builtStmt.append (where[i]);
      if (i != (where.size() - 1)) {
        rsql_builtStmt.append (" ");
      }
    }
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
        rc, fmt::format (
                "Could not compile statement: {} - {}",
                rsql_builtStmt, sqlite3_errmsg (db)
            )
    )};
  }

  if (!sqlite3_stmt_readonly (stmt)) {
    return std::unexpected{
        make_internal_err ("Statement would modify database.")
    };
  }

  return stmt;
}
