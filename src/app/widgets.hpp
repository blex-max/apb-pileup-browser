#pragma once

#include "shared/err.hpp"
#include "state.hpp"

VoidOrErr calc_static_widgets (TopUI& ui, const AppConfig& conf);
void draw_widgets (AppState& state);
