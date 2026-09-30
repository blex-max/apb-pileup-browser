#pragma once

#include <fmt/format.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

#include "log.hpp"
#include "termbox2.h"

// Fatal precondition/invariant and unreachable-code checks. On
// failure, restores the terminal via tb_shutdown() before
// aborting.
//
// call macros, not this fn
[[noreturn]] inline void apb_fatal (
    const char* file, int line, const std::string_view what,
    const std::optional<std::string_view> xtraInfo = std::nullopt
)
{
  tb_shutdown();
  const auto errmsg = fmt::format (
      "Program is ill-formed: {} at {}:{}"
      " - report to maintainer\n{}",
      what, file, line, (xtraInfo) ? *xtraInfo : ""
  );
  apb_log::log_push (errmsg);
  std::cerr
      << errmsg
      << "\n\nFlushing logs. If logging to file is not enabled, will "
         "attempt to "
         "write crash log at "
      << apb_log::crashLogPath << std::endl;
  apb_log::dump();
  std::abort();
}

#define APB_ASSERT(cond, ...)                                    \
  do {                                                           \
    if (!(cond)) {                                               \
      apb_fatal (                                                \
          __FILE__, __LINE__,                                    \
          "assertion failed: " #cond __VA_OPT__ (                \
              , apb_log::format_vars (#__VA_ARGS__, __VA_ARGS__) \
          )                                                      \
      );                                                         \
    }                                                            \
  } while (false)

#define APB_UNREACHABLE(msg) apb_fatal (__FILE__, __LINE__, (msg))
