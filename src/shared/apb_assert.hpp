#pragma once

#include <fmt/format.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "termbox2.h"

// Fatal precondition/invariant and unreachable-code checks. On
// failure, restores the terminal via tb_shutdown() before
// aborting. `varNames` (as captured by #__VA_ARGS__, e.g. "a, b")
// is paired with each of `values`, appended to the report as
// "a: 1, b: 2".
//
// call macros, not this fn
template <typename... Args>
[[noreturn]] void apb_fatal (
    const char* file, int line, const std::string_view what,
    const std::string_view varNames = "", const Args&... values
)
{
  tb_shutdown();

  std::string varInfo;
  size_t pos = 0;
  auto appendNext = [&] (const auto& value) {
    auto comma = varNames.find (',', pos);
    auto name = varNames.substr (
        pos, comma == std::string_view::npos ? std::string_view::npos
                                             : comma - pos
    );
    while (!name.empty() && name.front() == ' ') {
      name.remove_prefix (1);
    }
    if (!varInfo.empty()) {
      varInfo += ", ";
    }
    varInfo += fmt::format ("{}={}", name, value);
    pos = (comma == std::string_view::npos) ? varNames.size() : comma + 1;
  };
  (appendNext (values), ...);

  std::cerr << fmt::format (
      "\n{}:{}\nFatal error: {}\n", file, line, what
  );
  if (!varInfo.empty()) {
    std::cerr << varInfo << "\n";
  }
  std::cerr << " - report to maintainer\n\nnow aborting" << std::endl;
  std::abort();
}

#define APB_ASSERT(cond, ...)                     \
  do {                                            \
    if (!(cond)) {                                \
      apb_fatal (                                 \
          __FILE__, __LINE__,                     \
          "assertion failed: " #cond __VA_OPT__ ( \
              , #__VA_ARGS__, __VA_ARGS__         \
          )                                       \
      );                                          \
    }                                             \
  } while (false)

#define APB_UNREACHABLE(msg) apb_fatal (__FILE__, __LINE__, (msg))
