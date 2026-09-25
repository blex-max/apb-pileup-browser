#pragma once

#include <fmt/format.h>

#include <cstdlib>
#include <string_view>

#include "termbox2.h"

// Fatal precondition/invariant and unreachable-code checks. On
// failure, restores the terminal via tb_shutdown() before
// aborting.

// Implementation detail of APB_ASSERT/APB_UNREACHABLE below - call
// those instead, not this directly, so file/line reflect the
// actual call site.
[[noreturn]] inline void apb_fatal (
    std::string_view what, const char* file, int line
)
{
  tb_shutdown();
  fmt::print (
      stderr,
      "Invariant violated, program is ill-formed: {} at {}:{}"
      " - report to maintainer\n",
      what, file, line
  );
  std::abort();
}

#define APB_ASSERT(cond)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      apb_fatal ("assertion failed: " #cond, __FILE__, __LINE__); \
    }                                                             \
  } while (false)

#define APB_UNREACHABLE(msg) apb_fatal ((msg), __FILE__, __LINE__)
