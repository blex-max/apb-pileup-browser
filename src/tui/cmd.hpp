#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

struct CmdResult {
  bool success;
  std::string msg;
};

// view type into the command registry
struct CmdView {
  std::string_view call;
  std::span<const std::string_view> alias;  // empty if none
  CmdResult (*exec) (std::string_view);
  std::string_view usage;
  std::string_view desc;
};

CmdResult exec_cmd (std::string_view call);

// Exposed so manual.cpp can build
// Command Reference table.
std::vector<std::string> build_cmd_ref_table();
