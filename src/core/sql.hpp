#pragma once

#include <string_view>

// NOTE: rsql_ == raw sql string


// metadata table storing provenance data, possibly
// information retreived from the header
// NOTE: currently unused
inline constexpr std::string_view rsql_CreateMetaDataTable = R"sql(
CREATE TABLE sample (
    path TEXT -- alignment file path
)
)sql";


// Default temp_store spills large sorts/temp b-trees to disk. Everything in
// this db is meant to live in memory only — force temp structures there too,
// so a big sort can't fail with a disk I/O error on a machine with no disk
// space but plenty of RAM.
inline constexpr std::string_view rsql_SetTempStoreMemory = "PRAGMA temp_store = MEMORY;";


// One row per read overlapping pileup reference position.
inline constexpr std::string_view rsql_CreateReadsTable = R"sql(
CREATE TABLE reads (
    id          INTEGER PRIMARY KEY,

    -- pileup position fields
    qname       TEXT,  -- Query template NAME
    flag        INTEGER NOT NULL,  -- bitwise FLAG
    rstart      INTEGER NOT NULL,  -- 0-based leftmost mapping pos
    rend        INTEGER NOT NULL,  -- 0-based righmost mapping pos
    mapq        INTEGER NOT NULL,  -- MAPping Quality

    base        CHAR(1) NOT NULL,  -- query base at pileup position (denormalised from seq for easy access)
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
    tags        TEXT CHECK (json_valid (tags))
    -- NOTE: may add cigar as uint8_t blob for iterating/aligning on the query side

);
)sql";


// --- STATEMENTS ---

inline constexpr std::string_view rsql_InsertReads = R"sql(
  INSERT INTO reads (
      qname, flag, rstart, rend, mapq,
      base, basequal, qpos, indel, is_del, is_head, is_tail, is_refskip,
      cigar, seq, qual, mtid, mstart, tags
  ) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);
)sql";
