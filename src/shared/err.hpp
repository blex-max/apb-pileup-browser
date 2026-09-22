#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

enum class ErrSrc : uint8_t {
  htslib,
  sqlite,
  argparse,
  internal
};

enum class ErrKind : uint8_t {
  fatal  // only kind as of now
  // but in future may have recoverable errors
};

struct Err {
  ErrKind kind;
  ErrSrc src;
  std::optional<int>
      code;  // raw htslib/sqlite3 code, for diagnostics
  std::string msg;  // human-readable, for reporting
};

using VoidOrErr = std::expected<void, Err>;
using BoolOrErr = std::expected<bool, Err>;

inline Err make_htslib_err (const int code, std::string msg)
{
  return Err{
      ErrKind::fatal, ErrSrc::htslib, code, std::move (msg)
  };
}

inline Err make_sqlite3_err (const int code, std::string msg)
{
  return Err{
      ErrKind::fatal, ErrSrc::sqlite, code, std::move (msg)
  };
}

inline Err make_internal_err (std::string msg)
{
  return Err{
      ErrKind::fatal, ErrSrc::internal, std::nullopt,
      std::move (msg)
  };
}
