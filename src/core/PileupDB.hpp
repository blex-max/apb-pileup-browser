#pragma once

#include <expected>
#include <string>

#include "core/err.hpp"
#include "core/hts_types.hpp"
#include "core/sql_types.hpp"

struct PileupDB : public SqliteConn {
  friend std::expected<PileupDB, Err> make_db();

 private:
  PileupDB() = default;  // factory-only construction
};
using DbOrErr = std::expected<PileupDB, Err>;
DbOrErr make_db(); // factory

// insert sample metadata into database, returning id.
[[nodiscard]] IntOrErr insert_sample(
    PileupDB& db, const AlnFile& aln
);

// insert reads at pileup position into database
[[nodiscard]] VoidOrErr insert_pileup(
    PileupDB& db, const AlnFile& aln, const PileupPosition& pos,
    int alnId
);

// Copy the in-memory database out to a file on disk, using
// sqlite3's online backup API.
[[nodiscard]] VoidOrErr dump_to_disk(
    const PileupDB& db, const std::string& path
);

// TODO: undefined!
[[nodiscard]] VoidOrErr clear(PileupDB& db);
