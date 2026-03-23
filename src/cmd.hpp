#pragma once

#include <string_view>
#include <string>

namespace cmd {

struct CmdResult {
  bool success;
  std::string msg;
};
CmdResult exec_cmd (std::string_view call);
}
