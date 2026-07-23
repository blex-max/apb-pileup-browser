#pragma once

#include <string>
#include <vector>

#include "frontend/input.hpp"

struct CmdHistory {
  std::vector<std::string> entries;
  size_t idx = 0;  // == entries.size() means "at the live line"
  std::string
      draft;  // in-progress text stashed when browsing starts
};

void history_push (CmdHistory& h, std::string entry);
void history_prev (CmdHistory& h, EditBuf& buf);
void history_next (CmdHistory& h, EditBuf& buf);
