#pragma once

#include "app/helpblocks.hpp"

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

void draw_main_to_buffer();
// draw (sized and set) overlay widget.
// asserts required preconditions, otherwise
// should not fail, so void return.
void draw_overlay_to_buffer();
