#pragma once

#include <string>

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
        extb::Box display;
        extb::Point input_caret;
        extb::Box input_line;
        extb::Box status_line;
        std::string cmd_buf;
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

    static Context& create
    (extb::Box display_, extb::Point input_caret_, extb::Box input_line_, extb::Box status_line_) {
        static Context ctx(display_, input_caret_, input_line_, status_line_);
        return ctx;
    }

    private:
    explicit Context
    (extb::Box display_, extb::Point input_caret_, extb::Box input_line_, extb::Box status_line_)
    : ui{display_, input_caret_, input_line_, status_line_} {}
};

Context& init ();
void loop (Context& ctx);

} // end namespace


