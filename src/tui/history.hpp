#pragma once

#include <string>
#include <vector>

#include "input.hpp"

//FIXME: merge this TU with input
struct CmdHistory {
  std::vector<std::string> entries;
  size_t idx = 0;  // == entries.size() means at input line
  std::string draft;  // in-progress text stashed when browsing starts
};

void history_push (CmdHistory& h, std::string entry);
void history_prev (CmdHistory& h, EditBuf& buf);
void history_next (CmdHistory& h, EditBuf& buf);
