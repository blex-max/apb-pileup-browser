#pragma once

#include <string>
#include <string_view>
#include <vector>

std::string exec_cmd (std::string_view call);

// Exposed so manual.cpp can build
// Command Reference table.
std::vector<std::string> build_cmd_ref_table();
