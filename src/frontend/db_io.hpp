#pragma once

#include <sqlite3.h>

#include <string>
#include <string_view>

#include "shared/cleanup.hpp"

// Copy the in-memory database out to a file on disk, using
// sqlite3's online backup API.
// Returns sqlite3 integer status code.
[[nodiscard]] inline int dump_to_disk (
    sqlite3* conn, std::string_view path
)
{
  sqlite3* o_dumpConn = nullptr;
  sqlite3_backup* o_backupConn = nullptr;
  Defer dumpCleanup ([&]() {
    sqlite3_backup_finish (o_backupConn);
    sqlite3_close_v2 (o_dumpConn);
  });

  if (const auto rc =
          sqlite3_open (std::string{path}.c_str(), &o_dumpConn);
      rc != SQLITE_OK) {
    return rc;
  }

  if (o_backupConn =
          sqlite3_backup_init (o_dumpConn, "main", conn, "main");
      o_backupConn == NULL) {
    const auto rc = sqlite3_errcode (o_dumpConn);
    return rc;
  }

  if (const auto rc = sqlite3_backup_step (o_backupConn, -1);
      rc != SQLITE_DONE) {
    return rc;
  }

  return SQLITE_OK;
}
