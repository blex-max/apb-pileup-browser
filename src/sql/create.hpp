#include <expected>
#include <sqlite3.h>
#include <string_view>

#include "hts/pileup.hpp"
#include "sql/types.hpp"

namespace pileupsql {


// NOTE: not bundling metadata
// (i.e. position) with db
// since I expect to cache
// using metadata as key anyway.
// In other words, avoid bundling
// without demonstrable need even though
// these things go together.
// NOTE: What about making the database
// the cache directly? Have a keying table
// of position + contig (and possibly span for convienience),
// and let that table point you to the appropriate subtable?
// To discuss with claude or colleagues
struct PileupDB : public SqliteConn {};

using VoidOrSqliteErr = std::expected<void, SqliteErr>;
VoidOrSqliteErr init (PileupDB& db, std::string_view path);
VoidOrSqliteErr clear (PileupDB& db);
VoidOrSqliteErr insert_pileup (PileupDB& db, const PileupBundle& raw);

}

