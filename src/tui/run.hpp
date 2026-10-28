#pragma once

#include <sqlite3.h>

// run apb TUI.
// prints errors to stderr.
// returns exit_success/exit_failure.
[[nodiscard]] int run_tui (sqlite3* conn, bool verboseLog);
