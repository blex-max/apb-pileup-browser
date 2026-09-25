#pragma once

#include <sqlite3.h>


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
