#pragma once

#include "app/helpblocks.hpp"
#include "frontend/extb/extb.hpp"

namespace e2 = extb;

// Calculates and sets sizes on statically-sized UI elements.
// Returns true on success, false on failure due
// to insufficient space available.
[[nodiscard]] bool size_widgets();
// Calculates and sets sizes on dynamically-sized overlay widget.
// Returns true on success, false on failure due
// to insufficient space available.
[[nodiscard]] bool size_and_set_overlay_widget (
    helpblocks::TextBlockRef content
);

// NOTE: this reasonably well scoped shared err type is working
// quite well so far
// NOTE: also starting to think (optional) output fill err parameters
// are a good idea...
// NOTE: A Zig-like extensible global error union thing might be nice.
// Maybe a shared err namespace with a success state in it? but then I doubt
// you can extend the enum in various parts of the codebase independently...
struct WidgetStatus {
  enum Code : uint8_t {
    success,
    insufficientSz,
  };
  Code code;
};
[[nodiscard]] WidgetStatus draw_main_ui();
// draw (sized and set) overlay widget.
// asserts required preconditions, otherwise
// should not fail, so void return.
void draw_overlay();
