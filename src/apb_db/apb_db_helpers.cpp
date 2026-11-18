#include "apb_db_helpers.hpp"

#include <sqlite3.h>

#include <cstddef>

#include "utility/cleanup.hpp"

int apb_copy_db (sqlite3* connSrc, sqlite3* connDest)
{
  sqlite3_backup* o_backup = nullptr;
  Defer backup_cleanup ([&]() { sqlite3_backup_finish (o_backup); });

  if (o_backup = sqlite3_backup_init (connDest, "main", connSrc, "main");
      o_backup == NULL) {
    return sqlite3_errcode (connDest);
  }

  if (const auto rc = sqlite3_backup_step (o_backup, -1);
      rc != SQLITE_DONE) {
    return rc;
  }

  return SQLITE_OK;
}
