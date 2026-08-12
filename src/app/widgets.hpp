#pragma once

#include "shared/err.hpp"
#include "state.hpp"


void set_overlay_widget (TopUI& ui, TextBlockRef content);

VoidOrErr size_widgets (TopUI& ui, const AppConfig& conf);

void size_browser_panes (PileupWgt& pWgt, const AppConfig& conf);

void draw_widgets (AppState& state);
