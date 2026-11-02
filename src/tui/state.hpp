#pragma once

namespace tui_state {

inline constinit bool run = true;

// shutdown tui
// does not shutdown db,
// as tui does not own.
void tui_shutdown();

}  // namespace tui_state
