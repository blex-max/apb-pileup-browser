#pragma once

#include <string_view>


namespace pileupsql {

// One row per read overlapping pileup reference position.
// Repopulated (DELETE + INSERT) every time the cursor moves to a new column.
inline constexpr std::string_view stmtCreateReadsTable = R"sql(
CREATE TABLE reads (
    id          INTEGER PRIMARY KEY,  -- possibly unnecessary

    -- pileup position fields
    base        CHAR(1) NOT NULL,  -- query base at pileup position (denormalised from seq for easy access)
    basequal    INTEGER NOT NULL,  -- query base quality
    qpos        INTEGER NOT NULL,  -- 0-based offset into seq/qual at this position
    indel       INTEGER NOT NULL,  -- indel length to the next position (0 none, >0 ins, <0 del)
    is_del      INTEGER NOT NULL CHECK (is_del IN (0, 1)),
    is_head     INTEGER NOT NULL CHECK (is_head IN (0, 1)),
    is_tail     INTEGER NOT NULL CHECK (is_tail IN (0, 1)),
    is_refskip  INTEGER NOT NULL CHECK (is_refskip IN (0, 1)),

    -- bam1_t/alignment fields
    qname       TEXT    NOT NULL,  -- Query template NAME
    flag        INTEGER NOT NULL,  -- bitwise FLAG
    pos         INTEGER NOT NULL,  -- 0-based leftmost mapping POSition
    mapq        INTEGER NOT NULL,  -- MAPping Quality
    cigar       TEXT    NOT NULL,  -- CIGAR string, stored as text for querying purposes (* if NULL)
    seq         TEXT    NOT NULL,  -- segment SEQuence (* if NULL)
    qual        TEXT    NOT NULL,  -- ASCII of Phred-scaled base QUALity+33 (* if NULL)
    mtid        TEXT    NOT NULL,  -- Ref name of the mate/next read ('=' or '*' as per spec)
    mpos        INTEGER,  -- 0-based leftmost mappig position of the mate/next read, can be null

    -- Aux tags serialized as a JSON
    -- e.g. {"NM":2,"MD":"76","RG":"sample1"}. Query individual tags with
    -- json_extract(tags, '$.NM'). NULL if the read has no aux tags.
    tags        TEXT

);
)sql";

}  // namespace pileupsql
