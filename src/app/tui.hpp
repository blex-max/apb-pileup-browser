#pragma once

#include <expected>

#include "app/state.hpp"
#include "shared/err.hpp"

using AppStateOrErr = std::expected<AppState, Err>;
AppStateOrErr init();  // caller owns state, to be passed through
VoidOrErr loop (AppState& state);
void shutdown (AppState& state);
