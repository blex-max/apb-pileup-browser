#pragma once

#include <sqlite3.h>

#include <cstdint>

// Returns SQLITE_OK on success, sqlite err code on failure.
[[nodiscard]] int apb_copy_db (sqlite3* connSrc, sqlite3* connDest);


// enum corresponds to sqlite3 fundamental
// datatype integer codes
struct apb_col_type_t {
  enum type_t : uint8_t {
    sqlite_int = 1,
    sqlite_float,
    sqlite_text,
    sqlite_blob,
    sqlite_null,
  };
};
