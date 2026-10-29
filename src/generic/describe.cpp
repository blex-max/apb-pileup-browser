#include "describe.hpp"

#include <fmt/format.h>
#include <sqlite3.h>

std::string apb_describe_sqlite_failure (
    std::string_view what, int rc, std::optional<std::string_view> conn_msg
)
{
  switch (rc & 0xFF) {  // strip extended result code
    case SQLITE_CANTOPEN:
      return fmt::format (
          "{}: could not open file ({}).", what, sqlite3_errstr (rc)
      );
    case SQLITE_PERM:
    case SQLITE_READONLY:
      return fmt::format (
          "{}: permission denied ({}).", what, sqlite3_errstr (rc)
      );
    case SQLITE_NOTADB:
    case SQLITE_CORRUPT:
      return fmt::format (
          "{}: not a valid sqlite3 "
          "database, or is corrupt ({}).",
          what, sqlite3_errstr (rc)
      );
    case SQLITE_FULL:
    case SQLITE_IOERR:
      return fmt::format (
          "{}: a disk I/O error occurred ({}).", what, sqlite3_errstr (rc)
      );
    case SQLITE_NOMEM:
      return fmt::format (
          "{}: out of memory. Were you trying to load a very "
          "large pileup?",
          what
      );
    default:
      return fmt::format (
          "{}, reporting code {} and status {} - "
          "please report this failure to the maintainer.",
          what, rc, conn_msg.value_or (sqlite3_errstr (rc))
      );
  }
}
