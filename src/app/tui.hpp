#pragma once

#include <expected>

#include "app/state.hpp"
#include "backend/PileupDB.hpp"
#include "shared/err.hpp"

using AppStateOrErr = std::expected<AppState, Err>;
AppStateOrErr init (
    PileupDB& db
);  // caller owns state, to be passed through
VoidOrErr loop (AppState& state);
void shutdown();
