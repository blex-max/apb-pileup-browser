#pragma once

#include "shared/err.hpp"
#include "state.hpp"

VoidOrErr calc_all_widgets (TopUI& ui, const AppConfig& conf);
VoidOrErr calc_pileup_child_widgets (
    PileupWgt& main, const AppConfig& conf
);
void draw_widgets (AppState& state);
