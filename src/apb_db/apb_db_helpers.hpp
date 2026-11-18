#pragma once

#include <sqlite3.h>

// Returns SQLITE_OK on success, sqlite err code on failure.
[[nodiscard]] int apb_copy_db (sqlite3* connSrc, sqlite3* connDest);
