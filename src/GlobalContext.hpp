#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

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
        bool run = true;
        std::unordered_map<std::string_view, std::string(*)()> debug{};
        size_t frame = 0;
    } data;
    struct {
        extb::Box main;
        extb::Cell cmd_caret;
        extb::Box cmd;
        extb::Box status;
        input::EditBuf cmd_buf;
        std::string status_buf;
    } ui;
};
