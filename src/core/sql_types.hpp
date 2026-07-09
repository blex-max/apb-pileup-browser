#pragma once

#include <sqlite3.h>


struct SqliteConn {
  sqlite3* o_ptr = nullptr;
  operator sqlite3* () const { return o_ptr; }  // avoid having to route through a layer to access ptr

  SqliteConn () = default;
  // delete copy, avoid double free/close
  SqliteConn (const SqliteConn&) = delete;
  SqliteConn& operator= (const SqliteConn&) = delete;

  SqliteConn (SqliteConn&& other) noexcept : o_ptr (other.o_ptr) { other.o_ptr = nullptr; }
  SqliteConn& operator= (SqliteConn&& other) noexcept {
    if (this != &other) {
      if (o_ptr) sqlite3_close_v2 (o_ptr);
      o_ptr = other.o_ptr;
      other.o_ptr = nullptr;
    }
    return *this;
  }

  ~SqliteConn () { if (o_ptr) sqlite3_close_v2 (o_ptr); }  // v2: never leaks even if a stmt/backup is still outstanding; can't report an error from a dtor anyway
};


/* STATEMENTS */

template <typename StmtTag>
struct SqliteStmt {
  sqlite3_stmt* o_ptr = nullptr;
  operator sqlite3_stmt* () const { return o_ptr; }

  SqliteStmt () = default;
  SqliteStmt (const SqliteStmt&) = delete;
  SqliteStmt& operator= (const SqliteStmt&) = delete;

  SqliteStmt (SqliteStmt&& other) noexcept : o_ptr (other.o_ptr) { other.o_ptr = nullptr; }
  SqliteStmt& operator= (SqliteStmt&& other) noexcept {
    if (this != &other) {
      sqlite3_finalize (o_ptr);
      o_ptr = other.o_ptr;
      other.o_ptr = nullptr;
    }
    return *this;
  }

  ~SqliteStmt () { sqlite3_finalize (o_ptr); }
};

