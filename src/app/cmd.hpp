#pragma once

#include <span>
#include <string>
#include <string_view>

#include "app/state.hpp"

struct CmdResult {
  bool success;
  std::string msg;
};

// view type into the command registry
struct CmdView {
  std::string_view call;
  std::span<const std::string_view> alias;  // empty if none
  CmdResult (*exec) (std::string_view, AppState&);
  std::string_view usage;
  std::string_view desc;
};

CmdResult exec_cmd (std::string_view call, AppState& state);

// registry exposed so manual.cpp can build
// Command Reference table.
std::span<const CmdView* const> get_cmd_registry();
