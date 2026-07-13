#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "ctx.hpp"
#include "extb/extb-box.hpp"
#include "extb/extb.hpp"
#include "input.hpp"

enum class app_state : uint8_t { pileup };

struct GlobalContext : ctx::Context {
  struct {
    app_state state = app_state::pileup;
    size_t frame = 0;
  } data;
  struct {
    struct {
      extb::box::GlobalBox viewport;
      extb::box::GlobalBox frame;
    } main;
    struct {
      extb::box::GlobalBox display_line;
      extb::GlobalCell caret;
      extb::box::GlobalBox frame;
      input::EditBuf buf;
    } cmd;
    struct {
      extb::box::GlobalBox display_line;
      extb::box::GlobalBox frame;
      std::string buf;
    } status;
  } ui;
  struct {
    bool run = true;
    bool demo = true;
    std::list<std::string> debug_request;
  } conf;
};
