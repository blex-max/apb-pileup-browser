#pragma once

#include <string>
#include <string_view>

#include "backend/hts_types.hpp"
#include "backend/sql_types.hpp"
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
    PileupDB& db, const AlnFile& aln, const PileupPosition& pos
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

void select_all (const PileupDB&);
void select (
    const PileupDB& db, std::string_view where,
    std::string_view order_by
);
void count (const PileupDB& db, std::string_view where);
