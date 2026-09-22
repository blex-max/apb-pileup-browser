#pragma once

#include "app/state_components.hpp"
#include "app/widgets.hpp"

struct AppState {
  UIBundle ui;
  AppConfig conf;
  DBBundle db;
};
