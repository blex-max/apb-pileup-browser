#pragma once

#include <sqlite3.h>

struct SqliteConn {
  sqlite3* o_conn = nullptr;
  operator sqlite3*() const
  {
    return o_conn;
  }  // avoid having to route through a layer to access ptr

  SqliteConn() = default;
  // delete copy, avoid double free/close
  SqliteConn (const SqliteConn&) = delete;
  SqliteConn& operator= (const SqliteConn&) = delete;

  SqliteConn (SqliteConn&& other) noexcept
      : o_conn (other.o_conn)
  {
    other.o_conn = nullptr;
  }
  SqliteConn& operator= (SqliteConn&&) = delete;

  ~SqliteConn()
  {
    if (o_conn != nullptr) {
      sqlite3_close_v2 (o_conn);
    }
  }
};

/* STATEMENTS */

struct SqliteStmt {
  sqlite3_stmt* o_stmt = nullptr;
  operator sqlite3_stmt*() const { return o_stmt; }

  SqliteStmt() = default;
  SqliteStmt (const SqliteStmt&) = delete;
  SqliteStmt& operator= (const SqliteStmt&) = delete;

  SqliteStmt (SqliteStmt&& other) noexcept
      : o_stmt (other.o_stmt)
  {
    other.o_stmt = nullptr;
  }
  SqliteStmt& operator= (SqliteStmt&& other) noexcept
  {
    if (this != &other) {
      if (o_stmt != nullptr) {
        sqlite3_finalize (o_stmt);
      }
    }
    o_stmt = other.o_stmt;
    other.o_stmt = nullptr;
    return *this;
  }

  ~SqliteStmt()
  {
    if (o_stmt != nullptr) {
      sqlite3_finalize (o_stmt);
    }
  }
};
