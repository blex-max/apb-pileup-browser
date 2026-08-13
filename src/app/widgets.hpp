#pragma once

#include "shared/err.hpp"
#include "state.hpp"


void set_overlay_widget (TopUI& ui, TextBlockRef content);

VoidOrErr size_widgets (TopUI& ui, const AppConfig& conf);

void size_browser_panes (
    BrowserWgt& pWgt, const AppConfig& conf
);

VoidOrErr draw_main_ui (AppState& state);

void draw_overlay (const OverlayWgt& oWgt);
