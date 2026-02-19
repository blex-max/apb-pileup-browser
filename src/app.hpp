#pragma once

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


// singleton global context
struct Context {
    struct {
        app_state state = app_state::browse;
        bool run = true;
        bool debug = true;
        size_t frame = 0;
    } global;
    struct {
        extb::Box main;
        extb::Cell cmd_caret;
        extb::Box cmd;
        extb::Box status;
        std::string cmd_buf;
        std::string status_buf;
    } ui;
    struct {
        int row_sel = 0;
        PileupDisplayBundle pd;
    } browse_ctx;

    // no copies or moves
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    static Context& create () {
        static Context ctx;
        return ctx;
    }

    private:
    explicit Context () = default;
};

Context& init ();
void loop (Context& ctx);

} // end namespace


