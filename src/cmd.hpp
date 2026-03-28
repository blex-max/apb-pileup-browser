#pragma once

#include "GlobalContext.hpp"
#include <string_view>
#include <string>
#include <unordered_map>

namespace cmd {

// TODO reorganise this!
// I don't like that client code has to
// use the map directly.
inline std::string debug_print_frame () {
  auto frame = ctx::get<GlobalContext>().data.frame;
  return std::format("frame: {}", frame);
}
static std::unordered_map<std::string_view, std::string(*)()> DEBUG_CALLBACKS {
  {"frame", debug_print_frame}
};


struct CmdResult {
  bool success;
  std::string msg;
};
CmdResult exec_cmd (std::string_view call);
}
