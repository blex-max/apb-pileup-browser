#pragma once

#include <optional>
#include <string_view>
#include <string>

namespace cmd {

struct CmdResult {
  bool success;
  std::string msg;
};
CmdResult exec_cmd (std::string_view call);
std::optional<std::string> get_debug_text (std::string_view name);

}
