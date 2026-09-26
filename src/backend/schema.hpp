#pragma once

#include <string_view>

// NOTE: sqlite3 bind indexing is 1-based, select indexing
// is 0-based, so enum can only be used with select

namespace schema {

// keep all data in working memory.
inline constexpr std::string_view sqlSetTempStoreMemory =
    R"sql(
    PRAGMA temp_store = MEMORY;
)sql";


inline constexpr std::string_view sqlCreateMetaDataTable =
    R"sql(
CREATE TABLE metadata (
    id     INTEGER PRIMARY KEY CHECK (id = 1),  -- one row only; one locus per db
    apb_version TEXT NOT NULL,
    contig TEXT NOT NULL CHECK (contig <> ''),
    pos    INTEGER NOT NULL, -- 0-based pileup position
    start  INTEGER NOT NULL CHECK (start >= 0),
    end    INTEGER NOT NULL CHECK (start < end),
    ref    TEXT,             -- reference slice spanned by pileup
    CHECK (pos >= start AND pos <= end)
)
)sql";

inline constexpr std::string_view sqlInsertMetadata = R"sql(
INSERT INTO metadata (apb_version, contig, pos, start, end, ref) VALUES (?,?,?,?,?,?);
)sql";

struct MetaTableSelect {
  // TIED TO EXPLICIT SELECT STATEMENT
  enum Idx : uint8_t {
    id,
    version,
    contig,
    pos,
    start,
    end,
    ref,
  };

  // not a prefix, only one row, complete retrieval
  // and therefore semicolon terminated statement
  static constexpr std::string_view sql =
      R"sql(SELECT id, apb_version, contig, pos, start, end, ref FROM metadata;)sql";
};


// One row per read overlapping pileup reference position.
inline constexpr std::string_view sqlCreateReadsTable =
    R"sql(
CREATE TABLE reads (
    id          INTEGER PRIMARY KEY,

    -- pileup position fields
    qname       TEXT,  -- Query template NAME
    flag        INTEGER NOT NULL,  -- bitwise FLAG
    rstart      INTEGER NOT NULL,  -- 0-based leftmost mapping pos
    rend        INTEGER NOT NULL,  -- 0-based righmost mapping pos
    mapq        INTEGER NOT NULL,  -- MAPping Quality

    base        CHAR(1) NOT NULL CHECK (length (base) = 1),  -- query base at pileup position (denormalised from seq for easy access)
    basequal    INTEGER NOT NULL,  -- query base quality
    qpos        INTEGER NOT NULL,  -- 0-based offset into seq/qual at this position
    indel       INTEGER NOT NULL,  -- indel length to the next position (0 none, >0 ins, <0 del) NOTE: best format?
    is_del      INTEGER NOT NULL CHECK (is_del IN (0, 1)),
    is_head     INTEGER NOT NULL CHECK (is_head IN (0, 1)),
    is_tail     INTEGER NOT NULL CHECK (is_tail IN (0, 1)),
    is_refskip  INTEGER NOT NULL CHECK (is_refskip IN (0, 1)),

    -- bam1_t/alignment fields
    cigar       TEXT NOT NULL,  -- CIGAR string, stored as text for querying purposes
    seq         TEXT NOT NULL,  -- segment SEQuence
    qual        TEXT NOT NULL,  -- ASCII of Phred-scaled base QUALity+33

    mtid        TEXT,  -- Ref name of the mate/next read ('=' if same as tid per spec)
    mstart      INTEGER,  -- 0-based leftmost mappig position of the mate/next read, can be null

    -- Aux tags serialized as a JSON
    -- e.g. {"NM":2,"MD":"76","RG":"sample1"}. Query individual tags with
    -- json_extract(tags, '$.NM'). NULL if the read has no aux tags.
    tags        TEXT CHECK (json_valid (tags)),

    -- for frontend alignment purposes
    cig_uint32     BLOB NOT NULL,  -- can be null per spec, but that would be an unmapped read, which apb does not handle
    ncig        INTEGER NOT NULL,

    CHECK (length (seq) = length (qual)),
    CHECK (length (cig_uint32) = ncig * 4)
);
)sql";

inline constexpr std::string_view sqlInsertReads = R"sql(
INSERT INTO reads (
  qname, flag, rstart, rend, mapq,
  base, basequal, qpos, indel, is_del, is_head, is_tail, is_refskip,
  cigar, seq, qual, mtid, mstart, tags, cig_uint32, ncig
) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);
)sql";

struct ReadTableSelect {
  // TIED TO SCHEMA ORDER
  // query callers all use
  // SELECT * FROM reads - returns in schema order.
  enum Idx : uint8_t {
    id,  // 0
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

  // prefixes for dynamic querying, no terminating semicolon
  static constexpr std::string_view sqlPrefix =
      R"sql(SELECT * FROM reads)sql";
  static constexpr std::string_view sqlCountPrefix =
      R"sql(SELECT COUNT(*) FROM reads)sql";
};


}  // namespace schema
