#pragma once

#include "singleton.hpp"
#include <string>

#include <plog/Log.h>

#define TB_IMPL
#include "extb.hpp"
#include "hts/boundary-types.hpp"

namespace app {

constexpr auto CMD_H = 3;  // inc. borders
constexpr auto STATUS_H = 3;

enum class app_state : uint8_t {
  browse
};

struct GlobalContext : singleton::Singleton {
    public:
    struct {
        app_state state = app_state::browse;
        bool run = true;
        bool debug = true;
        size_t frame = 0;
    } data;
    struct {
        extb::Box main;
        extb::Cell cmd_caret;
        extb::Box cmd;
        extb::Box status;
        std::string cmd_buf;
        std::string status_buf;
    } ui;
};

struct PileupContext : singleton::Singleton {
    struct {
        int row_sel = 0;
        PileupDisplayBundle pd;
    } state;
    struct {
        extb::Box seq;
        extb::Box data;
    } ui;
};

void init ();
void loop ();

} // end namespace


