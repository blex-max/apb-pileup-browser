#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

struct dump_err_t {
  int rc;
  std::string msg;  // from the dump db handle, where the error lives
};
// Copy the in-memory database out to a file on disk, using
// sqlite3's online backup API.
[[nodiscard]] std::expected<void, dump_err_t> dump_to_disk (
    sqlite3* conn, std::string_view path
);

struct load_err_t {
  enum Kind : uint8_t {
    openFail,
    copyFail,
    contentCorrupt,
    appMismatch,
    verMismatch,
    schemaMismatch,
    sqlFail
  };
  Kind kind;
  std::optional<int> rc = std::nullopt;
  std::optional<std::string> msg = std::nullopt;
};
// Copy a database file on disk into a new in-memory apb database,
// using sqlite3's online backup API, and verify it for use with apb.
// Checks the file is not corrupt, is an apb database of the current
// schema, and has a locus metadata row. Well-formedness of that row
// is left to caller.
[[nodiscard]] std::expected<sqlite3*, load_err_t> load_from_disk (
    std::string_view path
);

// Formats a load_from_disk failure into a user-facing error.
std::string apb_describe_load_failure (
    std::string_view path, const load_err_t& err
);
