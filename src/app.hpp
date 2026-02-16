#pragma once

#include "tb.hpp"
#include <string>
#include <utility>

namespace app {

enum class app_state : uint8_t {
  cmd,
  browse,
  global
};

struct Context {
    struct {
        extb::Box display;
        extb::Box input_line;
        extb::Box return_line;
    } ui_elem;
    app_state state = app_state::browse;
    bool run = true;
    bool debug = true;
    size_t frame = 0;
};

using CmdResult = std::pair<bool, std::string>;
using Cmd = CmdResult(*)(Context&);

CmdResult cmd_quit (Context& ctx);

CmdResult exec_cmd (std::string_view cmd_name, Context& ctx);

} // end namespace


