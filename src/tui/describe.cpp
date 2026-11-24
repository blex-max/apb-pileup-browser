#include "describe.hpp"

#include <sqlite3.h>

std::string apb_describe_sqlite_failure (
    std::string_view what, int rc, std::optional<std::string_view> conn_msg
)
{
  std::string out{what};
  switch (rc & 0xFF) {  // strip extended result code
  case SQLITE_CANTOPEN:
    out += ": could not open file (";
    out += sqlite3_errstr (rc);
    out += ").";
    return out;
  case SQLITE_PERM:
  case SQLITE_READONLY:
    out += ": permission denied (";
    out += sqlite3_errstr (rc);
    out += ").";
    return out;
  case SQLITE_NOTADB:
  case SQLITE_CORRUPT:
    out += ": not a valid sqlite3 database, or is corrupt (";
    out += sqlite3_errstr (rc);
    out += ").";
    return out;
  case SQLITE_FULL:
  case SQLITE_IOERR:
    out += ": a disk I/O error occurred (";
    out += sqlite3_errstr (rc);
    out += ").";
    return out;
  case SQLITE_NOMEM:
    out += ": out of memory. Were you trying to load a very large pileup?";
    return out;
  case SQLITE_CONSTRAINT:
    out += ": a record failed apb's data checks (";
    out += conn_msg.value_or (sqlite3_errstr (rc));
    out +=
        "). The input may be malformed or corrupt; if you believe it is "
        "valid, please report this failure to the maintainer.";
    return out;
  default:
    out += ", reporting code ";
    out += std::to_string (rc);
    out += " and status ";
    out += conn_msg.value_or (sqlite3_errstr (rc));
    out += " - please report this failure to the maintainer.";
    return out;
  }
}
