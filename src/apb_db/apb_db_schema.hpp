// clang-format off
#pragma once

#include <cstdint>
#include <string_view>

#include "utility/stringify.hpp"

#define APB_SCHEMA_VER 3


// NOTE: sqlite3 bind indexing is 1-based, select indexing
// is 0-based, so enums are for use with with select statements only

namespace schema {

// NOTE: sqlite also provides PRAGMA schema_version,
// but this is actually used internally, so we
// use user_version, which is always safe.
inline constexpr std::string_view sqlSetVersion =
"PRAGMA user_version = " STR(APB_SCHEMA_VER) ";";

inline constexpr std::string_view sqlSetFK = R"sql(
PRAGMA foreign_keys = ON;
)sql";

// CHECKs repeated in apb_validate_db, as a read-only
// connection skips them, and in the TUI's
// PileupMetadata::valid(); keep all in sync
inline constexpr std::string_view sqlCreateLocusTable =
    R"sql(
CREATE TABLE locus_metadata (
     -- one row only; one locus per db
    id     INTEGER PRIMARY KEY CHECK (id = 1),

    -- locus
    -- string name of contig
    contig TEXT NOT NULL CHECK (contig <> ''),

    -- 1-based pileup position
    pileup_position    INTEGER NOT NULL CHECK (pileup_position > 0),

    -- 1-based leftmost and rightmost coordinate
    -- from stored records.
    records_start  INTEGER NOT NULL CHECK (records_start > 0),
    records_end    INTEGER NOT NULL CHECK (records_end >= records_start),

    -- reference slice over records_start..records_end
    ref    TEXT,

    -- number of record sources in db
    n_sources INTEGER NOT NULL CHECK (n_sources > 0),

    CHECK (pileup_position >= records_start AND pileup_position <= records_end),
    -- ref length in bytes, as consumers index it
    CHECK (ref IS NULL OR length (CAST (ref AS BLOB)) = records_end - records_start + 1)
) STRICT;
)sql";

inline constexpr std::string_view sqlInsertMetadata = R"sql(
INSERT INTO locus_metadata (contig, pileup_position, records_start, records_end, ref, n_sources) VALUES (?,?,?,?,?,?);
)sql";

struct MetaTableSelect {
  // TIED TO EXPLICIT SELECT STATEMENT
  enum Idx : uint8_t {
    id,
    contig,
    pileup_position,
    records_start,
    records_end,
    ref,
    n_sources,
  };

  // not a prefix, only one row, complete retrieval
  // and therefore semicolon terminated statement
  static constexpr std::string_view sql =
      R"sql(SELECT * FROM locus_metadata;)sql";
};

inline constexpr std::string_view sqlCreateSourceTable = R"sql(
  CREATE TABLE record_sources (
    id  INTEGER PRIMARY KEY,
    source TEXT NOT NULL UNIQUE,         -- source identifier, e.g. alignment file path or patient ID
    -- whether source has contig, to disambiguate contig presence with no records from contig absence
    has_contig INTEGER NOT NULL CHECK (
      has_contig IN (0, 1)
    )
  ) STRICT;
)sql";

inline constexpr std::string_view sqlInsertSource = R"sql(
INSERT INTO record_sources (source, has_contig) VALUES (?, ?);
)sql";

inline constexpr std::string_view sqlCreateContigTable = R"sql(
CREATE TABLE contigs (
  id      INTEGER PRIMARY KEY,
  contig  TEXT NOT NULL UNIQUE
) STRICT;
)sql";

inline constexpr std::string_view sqlInsertContig = R"sql(
INSERT INTO contigs (contig) VALUES (:contig);
)sql";

// NOTE: some of these fields may become deprecated
// as apb sqlite3 extensions are developed
inline constexpr std::string_view sqlCreatePileupTable = R"sql(
CREATE TABLE pileup_records (
    id          INTEGER PRIMARY KEY,
    -- base/basequal are derived on the reads view.
    qpos        INTEGER NOT NULL,          -- 1-based offset into seq/qual at this position, or next
                                           -- operation start postion iff base is deleted/refskip.
                                           -- NOT NULL even for del/refskip since examining read position
                                           -- bias may still be useful in these cases
    indel       INTEGER NOT NULL,          -- indel length to the next position (0 none, >0 ins, <0 del)
    -- booleans
    is_del      INTEGER NOT NULL CHECK (
      is_del IN (0, 1)
    ),
    is_refskip  INTEGER NOT NULL CHECK (
      is_refskip IN (0, 1)
    )
) STRICT;
)sql";

// One row per SAM record.
// In the future, this schema may become more extensible
inline constexpr std::string_view sqlCreateSAMTable =
    R"sql(
CREATE TABLE sam_records (
    id        INTEGER PRIMARY KEY,

    -- key SAM fields for apb --
    -- 1-based leftmost mapping pos
    start     INTEGER NOT NULL CHECK (
      start > 0
    ),
    -- 32-bit CIGAR operation array, as defined in htslib::sam.h
    cig32     BLOB NOT NULL CHECK (
      length (cig32) > 0
    ),
    -- SAM fields --
    -- seq/qual can be legitimately absent for
    -- e.g. an entirely hard clipped read
    seq       TEXT,                     -- base sequence
    -- base qualities as phred+33 ASCII, i.e. [0, 93] -> ['!', '~']
    qual      TEXT CHECK (
      qual IS NULL OR (qual <> '' AND qual NOT GLOB '*[^!-~]*')
    ),
    -- CIGAR string, denormalised as text for textual querying.
    cigar     TEXT CHECK (
      cigar <> ''
    ),

    -- 1-based righmost mapping pos
    end       INTEGER CHECK (
      end IS NULL OR end >= start
    ),
    qname     TEXT,                     -- Query template NAME
    -- 12-bit bitwise FLAG
    flag      INTEGER CHECK (
      flag IS NULL OR flag <= 4095
    ),
    -- mapping quality; NULL if unavailable (255 in SAM)
    mapq      INTEGER CHECK (
      mapq IS NULL OR mapq >= 0 AND mapq <= 254
    ),
    mtid_id   INTEGER REFERENCES contigs(id),
    -- 1-based leftmost mapping position of the mate/next read
    mstart    INTEGER CHECK (
      mstart IS NULL OR mstart > 0
    ),
    -- SAM Aux tags serialized as a JSON
    -- e.g. {"NM":2,"MD":"76","RG":"sample1"}. Query individual tags with
    -- json_extract(tags, '$.NM'). NULL if the read has no aux tags.
    tags      TEXT CHECK (
      json_valid (tags)
    ),

    pileup_id INTEGER UNIQUE REFERENCES pileup_records(id),
    source_id INTEGER NOT NULL REFERENCES record_sources(id),

    CHECK (seq IS NULL OR qual IS NULL OR length (seq) = length (qual))
) STRICT;
)sql";

// NOTE: when the viewer shows nearby reads, add
// `pileup_id IS NOT NULL AS aligned_to_locus` (shifts UserReadViewSelect)
inline constexpr std::string_view sqlCreateUserView = R"sql(
  CREATE VIEW reads AS
    SELECT
      r.*,
      -- '=' if mate on the db contig, per SAM RNEXT;
      -- all records share that contig
      CASE WHEN c.contig = (SELECT contig FROM locus_metadata)
        THEN '='
        ELSE c.contig
      END AS mtid,
      p.qpos, p.indel, p.is_del, p.is_refskip,
      -- base/basequal at pileup position; NULL if deleted/refskip or no pileup row
      CASE WHEN p.is_del = 0 AND p.is_refskip = 0
        THEN substr (r.seq, p.qpos, 1)
      END AS base,
      CASE WHEN p.is_del = 0 AND p.is_refskip = 0
        THEN unicode (substr (r.qual, p.qpos, 1)) - 33
      END AS basequal,
      s.source
    FROM sam_records r
    LEFT JOIN pileup_records p ON p.id = r.pileup_id
    LEFT JOIN record_sources s ON s.id = r.source_id
    LEFT JOIN contigs c ON c.id = r.mtid_id;
)sql";

// TODO: for col selection from VIEW:
// std::unordered_map<std::string, int> col_index (sqlite3_stmt* stmt)
// {
//   std::unordered_map<std::string, int> m;
//   const int n = sqlite3_column_count (stmt);
//   for (int i = 0; i < n; ++i) {
//     m.emplace (sqlite3_column_name (stmt, i), i);
//   }
//   return m;
// }

// Things to be aware of:

// - Name all expression columns with AS. SQLite only guarantees the result column name when there's an AS alias. For a bare column (SELECT start FROM ...) or SELECT * it uses the column name in practice. For an expression without an alias, the name is officially unspecified. The view already aliases base and basequal, so that's fine.
// - Duplicate names could clash

struct UserReadViewSelect {
  // TIED TO VIEW COLUMN ORDER
  // (sam_records columns, then joined columns).
  // query callers all use SELECT * FROM reads.
  enum Idx : uint8_t {
    id,  // 0
    start,
    cig32,
    seq,
    qual,
    cigar,
    end,
    qname,
    flag,
    mapq,
    mtid_id,
    mstart,
    tags,
    pileup_id,
    source_id,
    mtid,
    qpos,
    indel,
    is_del,
    is_refskip,
    base,
    basequal,
    source,
  };
};

}  // namespace schema
