#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "ctx.hpp"
#include "extb.hpp"
#include "input.hpp"


enum class app_state : uint8_t {
  browse
};

struct GlobalContext : ctx::Context {
    public:
    struct {
        app_state state = app_state::browse;
        size_t frame = 0;
    } data;
    struct {
        extb::GlobalBox view;
        extb::GlobalCell cmd_caret;
        extb::GlobalBox cmd;
        extb::GlobalBox status;
        input::EditBuf cmd_buf;
        std::string status_buf;
    } ui;
    struct {
        bool run = true;
        bool demo = true;
        std::list<std::string> debug_request;
    } conf;
};
