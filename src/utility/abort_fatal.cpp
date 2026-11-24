#include "abort_fatal.hpp"

#include <cstdlib>
#include <iostream>

void apb_abort_fatal_impl (
    const char* file, int line, std::string_view what,
    std::string_view varNames, std::initializer_list<std::string> values
)
{
  std::string varInfo;
  size_t pos = 0;
  for (const auto& value : values) {
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
    varInfo += std::string (name) + "=" + value;
    pos = (comma == std::string_view::npos) ? varNames.size() : comma + 1;
  }

  std::cerr << "\n"
            << file << ":" << line << "\nFatal error: " << what << "\n";
  if (!varInfo.empty()) {
    std::cerr << varInfo << "\n";
  }
  std::cerr << " - please report to maintainer\n\nnow aborting"
            << std::endl;
  std::abort();
}
