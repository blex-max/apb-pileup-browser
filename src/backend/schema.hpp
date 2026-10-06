#pragma once

#include <cstdint>
#include <string_view>

// Tests for this header live in schema.cpp (a test-only TU).

// NOTE: sqlite3 bind indexing is 1-based, select indexing
// is 0-based, so enum can only be used with select

namespace schema {

// keep all data in working memory.
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

    apb_version TEXT NOT NULL,

    -- locus
    contig TEXT NOT NULL CHECK (contig <> ''),
    -- 1-based pileup position
    pos    INTEGER NOT NULL CHECK (pos > 0),
    -- 1-based leftmost and rightmost coordinate
    -- from reads spanning the pileup.
    start  INTEGER NOT NULL CHECK (start > 0),
    end    INTEGER NOT NULL CHECK (start <= end),

    -- reference slice spanned by pileup
    ref    TEXT,
    
    CHECK (pos >= start AND pos <= end)
)
)sql";

inline constexpr std::string_view sqlInsertMetadata = R"sql(
INSERT INTO locus_metadata (apb_version, contig, pos, start, end, ref) VALUES (?,?,?,?,?,?);
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
      R"sql(SELECT id, apb_version, contig, pos, start, end, ref FROM locus_metadata;)sql";
};

inline constexpr std::string_view sqlCreateFileTable = R"sql(
  CREATE TABLE alignment_files (
    id  INTEGER PRIMARY KEY,
    path TEXT NOT NULL UNIQUE
  );
)sql";

inline constexpr std::string_view sqlInsertFile = R"sql(
  INSERT INTO alignment_files (path) VALUES (?);
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

    base        CHAR(1) CHECK (base IS NULL OR length (base) = 1),  -- query base at pileup position (denormalised from seq for easy access)
    basequal    INTEGER CHECK (basequal IS NULL OR (basequal > -10 AND basequal < 100)),  -- query base quality. Bounds checks conservative, see base-quality-ranges.txt in repo.
    qpos        INTEGER NOT NULL CHECK (qpos > 0),  -- 1-based offset into seq/qual at this position
    indel       INTEGER NOT NULL,  -- indel length to the next position (0 none, >0 ins, <0 del)
    is_del      INTEGER NOT NULL CHECK (is_del IN (0, 1)),
    is_head     INTEGER NOT NULL CHECK (is_head IN (0, 1)),
    is_tail     INTEGER NOT NULL CHECK (is_tail IN (0, 1)),
    is_refskip  INTEGER NOT NULL CHECK (is_refskip IN (0, 1)),

    -- bam1_t/alignment fields
    cigar       TEXT NOT NULL CHECK (cigar <> ''),  -- CIGAR string, stored as text for querying purposes
    -- these could legitamtely be empty for an entirely hard clipped read
    seq         TEXT NOT NULL,  -- segment SEQuence
    qual        TEXT NOT NULL,  -- ASCII of Phred-scaled base QUALity+33

    mtid        TEXT,  -- Ref name of the mate/next read ('=' if same as tid per spec)
    mstart      INTEGER CHECK (mstart IS NULL OR mstart > 0),  -- 1-based leftmost mapping position of the mate/next read, can be null

    -- Aux tags serialized as a JSON
    -- e.g. {"NM":2,"MD":"76","RG":"sample1"}. Query individual tags with
    -- json_extract(tags, '$.NM'). NULL if the read has no aux tags.
    tags        TEXT CHECK (json_valid (tags)),

    -- for frontend alignment purposes
    cig_uint32  BLOB NOT NULL,  -- can be null per spec, but that would be an unmapped read, which apb does not handle
    ncig        INTEGER NOT NULL,

    path_id INTEGER NOT NULL REFERENCES alignment_files(id),

    CHECK (length (seq) = length (qual)),
    CHECK (length (cig_uint32) = ncig * 4)
);
)sql";

// cross-table invariant a CHECK constraint can't express.
inline constexpr std::string_view sqlCreatePerRecordInsertTrigger =
    R"sql(
CREATE TRIGGER validate_read_span
AFTER INSERT ON per_record_data
FOR EACH ROW
BEGIN
  SELECT RAISE (ABORT, 'read span/position inconsistent with locus metadata')
  FROM locus_metadata
  WHERE NEW.start < locus_metadata.start
     OR NEW.end   > locus_metadata.end
     OR NEW.start > locus_metadata.pos
     OR NEW.end   < locus_metadata.pos;
END;
)sql";

inline constexpr std::string_view sqlInsertReads = R"sql(
INSERT INTO per_record_data (
  qname, flag, start, end, mapq,
  base, basequal, qpos, indel, is_del, is_head, is_tail, is_refskip,
  cigar, seq, qual, mtid, mstart, tags, cig_uint32, ncig, path_id
) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);
)sql";

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
    path,
  };

  // prefixes for dynamic querying, no terminating semicolon
  static constexpr std::string_view sqlPrefix =
      R"sql(SELECT * FROM reads)sql";
  static constexpr std::string_view sqlCountPrefix =
      R"sql(SELECT COUNT(*) FROM reads)sql";
};

}  // namespace schema
