#pragma once

#include <expected>

#include "app/state.hpp"
#include "backend/hts_sql.hpp"
#include "shared/err.hpp"

// Takes ownership of db, moves into output
// Returns initialised AppState on success,
// sqlite3 int return code on failure
std::expected<AppState, int> init_tui_state (
    PileupDB& db,
    std::optional<std::string_view> startupMsg = std::nullopt
);
VoidOrErr run_tui_loop (AppState& state);
