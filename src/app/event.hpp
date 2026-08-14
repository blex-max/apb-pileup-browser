#pragma once

#include "app/state.hpp"
#include "frontend/extb/extb.hpp"
#include "shared/err.hpp"

VoidOrErr handle_event (AppState& state, const tb_event& ev);
