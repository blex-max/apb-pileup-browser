#pragma once

#include <string>
#include <string_view>

#include "app/state.hpp"

struct CmdResult {
  bool success;
  std::string msg;
};

CmdResult exec_cmd (std::string_view call, AppState& state);
