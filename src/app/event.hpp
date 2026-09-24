#pragma once

#include "app/state.hpp"
#include "frontend/extb/extb.hpp"

TuiStatus handle_event (AppState& state, const tb_event& ev);
