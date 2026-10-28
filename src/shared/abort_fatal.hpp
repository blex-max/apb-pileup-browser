#pragma once

#include <fmt/format.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

// Report a fatal error to stderr and abort. `varNames` (as captured
// by #__VA_ARGS__, e.g. "a, b") is paired with each of `values`,
// appended to the report as "a=1" lines.
//
// call via assert macros (APB_ / TUI_), not directly
template <typename... Args>
[[noreturn]] void apb_abort_fatal (
    const char* file, int line, const std::string_view what,
    const std::string_view varNames = "", const Args&... values
)
{
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
      varInfo += "\n";
    }
    varInfo += fmt::format ("{}={}", name, value);
    pos = (comma == std::string_view::npos) ? varNames.size() : comma + 1;
  };
  // The most cpp thing ever: comma-fold expression.
  // The comma "operator" is overloaded and will apply
  // appendNext to each item in values (which can be any type).
  (appendNext (values), ...);

  std::cerr << fmt::format (
      "\n{}:{}\nFatal error: {}\n", file, line, what
  );
  if (!varInfo.empty()) {
    std::cerr << varInfo << "\n";
  }
  std::cerr << " - please report to maintainer\n\nnow aborting"
            << std::endl;
  std::abort();
}
