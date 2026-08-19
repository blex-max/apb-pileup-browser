#pragma once

#include <string_view>

inline constexpr std::string_view sh_sqlPragmaForeignKeys =
    R"sql(
    PRAGMA foreign_keys = ON;
)sql";

// Default temp_store spills large sorts/temp b-trees to disk. Everything in
// this db is meant to live in memory only — force temp structures there too,
// so a big sort can't fail with a disk I/O error on a machine with no disk
// space but plenty of RAM.
inline constexpr std::string_view sh_sqlSetTempStoreMemory =
    R"sql(
    PRAGMA temp_store = MEMORY;
)sql";

// metadata table storing provenance data, possibly
// information retreived from the header. Deliberately unlinked to
// loci/reads: one alignment file (and therefore one metadata row) per db.
inline constexpr std::string_view sh_sqlCreateMetaDataTable =
    R"sql(
CREATE TABLE metadata (
    id     INTEGER PRIMARY KEY,
    field1 INT NOT NULL -- placeholder
)
)sql";

inline constexpr std::string_view sh_sqlCreateLociTable =
    R"sql(
CREATE TABLE loci (
    id         INTEGER PRIMARY KEY,
    contig     TEXT NOT NULL,
    pos        INTEGER NOT NULL, -- 0-based pileup position
    start      INTEGER,
    end        INTEGER,
    ref        TEXT              -- reference slice spanned by pileup
)
)sql";

// One row per read overlapping pileup reference position.
inline constexpr std::string_view sh_sqlCreateReadsTable =
    R"sql(
CREATE TABLE reads (
    id          INTEGER PRIMARY KEY,
    loci_id     INTEGER NOT NULL REFERENCES loci(id) ON DELETE CASCADE,

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
    tags        TEXT CHECK (json_valid (tags)),

    -- for frontend alignment purposes
    cig_uint32     BLOB NOT NULL,
    ncig        INTEGER NOT NULL

);
)sql";

// Supports both the common "reads at this locus" query and cascade
// deletes (loci -> reads).
inline constexpr std::string_view sh_sqlCreateReadsLociIdIndex =
    R"sql(
CREATE INDEX idx_reads_loci_id ON reads(loci_id);
)sql";

// --- STATEMENTS ---
inline constexpr std::string_view sh_sqlInsertMetadata = R"sql(
INSERT INTO metadata (field1) VALUES (?);
)sql";

inline constexpr std::string_view sh_sqlInsertLoci = R"sql(
INSERT INTO loci (contig, pos, start, end, ref) VALUES (?,?,?,?,?);
)sql";

inline constexpr std::string_view sh_sqlSelectLoci = R"sql(
SELECT contig, pos, start, end, ref FROM loci;
)sql";

inline constexpr std::string_view sh_sqlInsertReads = R"sql(
INSERT INTO reads (
  loci_id,
  qname, flag, rstart, rend, mapq,
  base, basequal, qpos, indel, is_del, is_head, is_tail, is_refskip,
  cigar, seq, qual, mtid, mstart, tags, cig_uint32, ncig
) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);
)sql";
