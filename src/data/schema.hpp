// clang-format off
#pragma once

#include <cstdint>
#include <string_view>

#include "shared/stringify.hpp"

#define APB_APP_ID 777
#define APB_SCHEMA_VER 2


// NOTE: sqlite3 bind indexing is 1-based, select indexing
// is 0-based, so enums are for use with with select statements only

namespace schema {

// FIXME: not yet used
inline constexpr std::string_view sqlSetAppId = 
    "PRAGMA application_id = " STR(APB_APP_ID) ";";

inline constexpr std::string_view sqlSetVersion =
  "PRAGMA user_version = " STR(APB_SCHEMA_VER) ";";

// keep all data in RAM.
// Note that because we do this, some
// classic optimisation techniques,
// like using WAL, are irrelevant.
inline constexpr std::string_view sqlSetTempStoreMemory =
    R"sql(
    PRAGMA temp_store = MEMORY;
)sql";

inline constexpr std::string_view sqlSetFK =
    R"sql(
    PRAGMA foreign_keys = ON;
)sql";

inline constexpr std::string_view sqlCreateLocusTable =
    R"sql(
CREATE TABLE locus_metadata (
     -- one row only; one locus per db
    id     INTEGER PRIMARY KEY CHECK (id = 1),

    -- locus
    -- string name of contig
    contig TEXT NOT NULL CHECK (contig <> ''),
    -- 1-based pileup position
    pos    INTEGER NOT NULL CHECK (pos > 0),
    -- 1-based leftmost and rightmost coordinate
    -- from reads spanning the pileup.
    start  INTEGER NOT NULL CHECK (start > 0),
    end    INTEGER NOT NULL CHECK (start <= end),

    -- reference slice spanned by pileup
    ref    TEXT,

    -- number of alignment files represented in db
    n_aln INTEGER NOT NULL CHECK (n_aln > 0),
    
    CHECK (pos >= start AND pos <= end)
) STRICT;
)sql";

inline constexpr std::string_view sqlInsertMetadata = R"sql(
INSERT INTO locus_metadata (apb_db_version, contig, pos, start, end, ref, n_aln) VALUES (?,?,?,?,?,?,?);
)sql";

struct MetaTableSelect {
  // TIED TO EXPLICIT SELECT STATEMENT
  enum Idx : uint8_t {
    id,
    dbVersion,
    contig,
    pos,
    start,
    end,
    ref,
    n_aln,
  };

  // not a prefix, only one row, complete retrieval
  // and therefore semicolon terminated statement
  static constexpr std::string_view sql =
      R"sql(SELECT * FROM locus_metadata;)sql";
};

inline constexpr std::string_view sqlCreateFileTable = R"sql(
  CREATE TABLE alignment_files (
    id  INTEGER PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    has_contig INTEGER NOT NULL CHECK (has_contig IN (0, 1)),
    n_reads INTEGER NOT NULL CHECK (has_reads >= 0)
  ) STRICT;
)sql";

inline constexpr std::string_view sqlInsertFile = R"sql(
  INSERT INTO alignment_files (path, has_contig, has_reads) VALUES (?, ?, ?);
)sql";

// One row per read overlapping pileup reference position.
inline constexpr std::string_view sqlCreatePerRecordTable =
    R"sql(
CREATE TABLE per_record_data (
    id          INTEGER PRIMARY KEY,

    -- pileup position fields
    qname       TEXT,  -- Query template NAME
    flag        INTEGER NOT NULL,  -- bitwise FLAG
    start       INTEGER NOT NULL CHECK (start > 0),        -- 1-based leftmost mapping pos
    end         INTEGER NOT NULL CHECK (end >= start),     -- 1-based righmost mapping pos
    mapq        INTEGER NOT NULL CHECK (mapq >= 0 AND mapq <= 255),  -- MAPping Quality

    base        TEXT CHECK (base IS NULL OR length (base) = 1),  -- query base at pileup position (denormalised from seq for easy access)
    -- query base quality. SAM format spec specifies [0, 93], but see base-quality-ranges.txt in repo...
    basequal    INTEGER CHECK (basequal IS NULL OR (basequal >= 0 AND basequal <= 93)),  
    qpos        INTEGER NOT NULL CHECK (qpos > 0),  -- 1-based offset into seq/qual at this position
    indel       INTEGER NOT NULL,  -- indel length to the next position (0 none, >0 ins, <0 del)
    is_del      INTEGER NOT NULL CHECK (is_del IN (0, 1)),
    is_head     INTEGER NOT NULL CHECK (is_head IN (0, 1)),
    is_tail     INTEGER NOT NULL CHECK (is_tail IN (0, 1)),
    is_refskip  INTEGER NOT NULL CHECK (is_refskip IN (0, 1)),

    -- bam1_t/alignment fields
    cigar       TEXT NOT NULL CHECK (cigar <> ''),  -- CIGAR string, stored as text for querying purposes. Cannot be null/empty for a mapped read.
    -- these can legitamtely be empty for e.g. an entirely hard clipped read
    seq         TEXT,  -- segment SEQuence
    qual        BLOB,  -- array of base qualities [0, 93] as uint8_t

    mtid        TEXT CHECK (mtid IS NULL OR mtid <> ''),  -- Ref name of the mate/next read ('=' if same as tid per spec)
    mstart      INTEGER CHECK (mstart IS NULL OR mstart > 0),  -- 1-based leftmost mapping position of the mate/next read, can be null

    -- Aux tags serialized as a JSON
    -- e.g. {"NM":2,"MD":"76","RG":"sample1"}. Query individual tags with
    -- json_extract(tags, '$.NM'). NULL if the read has no aux tags.
    tags        TEXT CHECK (json_valid (tags)),

    -- for frontend alignment purposes
    cig_uint32  BLOB NOT NULL,  -- cannot be null for a mapped read
    ncig        INTEGER NOT NULL,

    path_id INTEGER NOT NULL REFERENCES alignment_files(id),

    CHECK (length (seq) = length (qual)),
    CHECK (base IS NULL OR seq IS NULL OR base = substr (seq, qpos, 1)),
    CHECK (basequal IS NULL OR qual IS NULL OR hex (substr (qual, qpos, 1)) = printf ('%02X', basequal)),
    CHECK (length (cig_uint32) = ncig * 4)
) STRICT;
)sql";

inline constexpr std::string_view sqlInsertReads = R"sql(
INSERT INTO per_record_data (
  qname, flag, start, end, mapq,
  base, basequal, qpos, indel, is_del, is_head, is_tail, is_refskip,
  cigar, seq, qual, mtid, mstart, tags, cig_uint32, ncig, path_id
) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);
)sql";

// FIXME: rename "reads" to "pileup"
inline constexpr std::string_view sqlCreateUserView = R"sql(
  CREATE VIEW reads AS
    SELECT r.*, f.path
    FROM per_record_data r JOIN alignment_files f ON f.id = r.path_id;
)sql";

struct UserReadViewSelect {
  // TIED TO SCHEMA ORDER
  // query callers all use
  // SELECT * FROM reads - returns in schema order.
  enum Idx : uint8_t {
    id,  // 0
    qname,
    flag,
    start,
    end,
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
    ncig,
    path_id,
    path,
  };

  // FIXME: it's beefier,
  // but it might make more sense perhaps to have fn ptrs
  // for each row retrieval.
};

}  // namespace schema
