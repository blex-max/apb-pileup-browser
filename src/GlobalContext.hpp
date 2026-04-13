#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "ctx.hpp"
#include "extb/extb-box.hpp"
#include "extb/extb.hpp"
#include "input.hpp"


enum class app_state : uint8_t {
  browse
};

using namespace extb;
using namespace extb::box;
struct GlobalContext : ctx::Context {
    struct {
        app_state state = app_state::browse;
        size_t frame = 0;
    } data;
    struct {
        struct {
            GlobalBox viewport;
            GlobalBox frame;
        } main;
        struct {
            GlobalBox display_line;
            GlobalCell caret;
            GlobalBox frame;
            input::EditBuf buf;
        } cmd;
        struct {
            GlobalBox display_line;
            GlobalBox frame;
            std::string buf;
        } status;
    } ui;
    struct {
        bool run = true;
        bool demo = true;
        std::list<std::string> debug_request;
    } conf;
};
