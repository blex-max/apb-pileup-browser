#include "create.hpp"

#include "hts/accessors.hpp"
#include "sql/schema.hpp"
#include "util.hpp"

namespace pileupsql {

VoidOrSqliteErr init (PileupDB& db, std::string_view path) {
  int fRc = 0;
  if (fRc = sqlite3_open (path.data(), &db.ptr); fRc != SQLITE_OK) {
    return std::unexpected{SqliteErr{fRc, std::string(sqlite3_errmsg (db))}};
  }

  char* tmpErr;
  if (fRc = sqlite3_exec(db, stmtCreateReadsTable.data(), nullptr, nullptr, &tmpErr); fRc != SQLITE_OK) {
    std::string fErr = tmpErr;
    sqlite3_free (tmpErr);
    return std::unexpected{SqliteErr{fRc, fErr}};
  }

  return {};
}


VoidOrSqliteErr clear (PileupDB& db) {
  int fRc = 0;
  char* tmpErr;

  if (fRc = sqlite3_exec(db, "DELETE FROM reads;", nullptr, nullptr, &tmpErr); fRc != SQLITE_OK) {
    std::string fErr = tmpErr;
    sqlite3_free (tmpErr);
    return std::unexpected{SqliteErr{fRc, fErr}};
  }

  return {};
}


VoidOrSqliteErr insert_pileup (PileupDB& db, const PileupBundle& raw) {
  /*
    Insert pileup data into sql database
  */

  // transaction controlled directly, rather than using autocommit for each row
  sqlite3_exec (db, "BEGIN;", nullptr, nullptr, nullptr);

  static constexpr const char* stmtInsertPileup = R"sql(
      INSERT INTO reads (
          base, basequal, qpos, indel, is_del, is_head, is_tail, is_refskip, cigar_ind,
          qname, flag, pos, mapq, cigar, mtid, mpos, seq, qual, tags
      ) VALUES (?,?,?,?,?,?,?,?,?, ?,?,?,?,?,?,?,?,?,?);
  )sql";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2 (db, stmtInsertPileup, -1, &stmt, nullptr) != SQLITE_OK) {
    // TODO: ERR
  }

  for (const bam_pileup1_t* p1 : raw.data) {
    // NOTE: database uses 0-based positions, like htslib internals
    // NOTE: statements in col order. col controls insertion position
    int col = 1;
    sqlite3_bind_int   (stmt, col++, htsacc::base(p1));
    sqlite3_bind_int   (stmt, col++, htsacc::base_qual(p1));
    sqlite3_bind_int   (stmt, col++, p1->qpos);
    sqlite3_bind_int   (stmt, col++, p1->indel);
    sqlite3_bind_int   (stmt, col++, p1->is_del);
    sqlite3_bind_int   (stmt, col++, p1->is_head);
    sqlite3_bind_int   (stmt, col++, p1->is_tail);
    sqlite3_bind_int   (stmt, col++, p1->is_refskip);
    sqlite3_bind_int   (stmt, col++, p1->cigar_ind);
    sqlite3_bind_text  (stmt, col++, bam_get_qname (p1->b), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (stmt, col++, htsacc::flag(p1));
    sqlite3_bind_int64 (stmt, col++, htsacc::start(p1));
    sqlite3_bind_int   (stmt, col++, htsacc::mapq(p1));
    sqlite3_bind_text  (stmt, col++, /* cigar      */ "", -1, SQLITE_TRANSIENT);  // TODO: stringify bam_get_cigar(p1->b)
    sqlite3_bind_text  (stmt, col++, /* mtid       */ "", -1, SQLITE_TRANSIENT);  // TODO: needs header — not reachable here yet
    sqlite3_bind_int64 (stmt, col++, htsacc::mpos(p1));
    sqlite3_bind_text  (stmt, col++, htsacc::seq(p1).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (stmt, col++, htsacc::qual_ascii(p1).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_null  (stmt, col++);  // tags — TODO: aux -> JSON

    // NOTE: below is unreviewed AI code
    if (sqlite3_step (stmt) != SQLITE_DONE) {
        sqlite3_finalize (stmt);
        sqlite3_exec (db, "ROLLBACK;", nullptr, nullptr, nullptr);
        // TODO: ERR
    }
    sqlite3_reset (stmt);
    sqlite3_clear_bindings (stmt);
  }

  sqlite3_finalize (stmt);
  sqlite3_exec (db, "COMMIT;", nullptr, nullptr, nullptr);

  return {};
}

} // end namespace

