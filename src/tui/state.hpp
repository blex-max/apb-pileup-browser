#pragma once

#include <cstdint>
#include <string_view>

namespace tui_state {

inline constinit bool run = true;

enum class focus_mode_t : uint8_t { browserMode, promptMode, overlayMode };
inline std::string_view to_string (focus_mode_t mode)
{
  switch (mode) {
  case focus_mode_t::browserMode:
    return "browser";
  case focus_mode_t::promptMode:
    return "prompt";
  case focus_mode_t::overlayMode:
    return "overlay";
  }
}
inline constinit focus_mode_t focus = focus_mode_t::promptMode;


// shutdown tui
// does not shutdown db,
// as tui does not own.
void tui_shutdown();

}  // namespace tui_state
