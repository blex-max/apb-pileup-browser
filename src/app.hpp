#pragma once

#include <string>

#include "extb.hpp"

namespace app {

enum class app_state : uint8_t {
  cmd,
  browse,
  global
};

struct Context {
    struct {
        app_state state = app_state::browse;
        bool run = true;
        bool debug = true;
        size_t frame = 0;
    } global;
    struct {
        extb::Box display;
        extb::Box input_line;
        extb::Box status_line;
        std::string cmd_buf;
    } ui;
    struct {
        int row_sel = 0;
    } browse_ctx;
};

void loop (Context& ctx);

} // end namespace


