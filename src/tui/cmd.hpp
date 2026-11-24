#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

enum class ExecStatus : uint8_t { success, fail, unknown };
std::pair<ExecStatus, std::string> exec_cmd (std::string_view call);

// The display toggle named by an unambiguous prefix of
// a `toggle` option, or an error message.
std::expected<bool*, std::string> find_toggle (std::string_view name);

// Exposed so manual.cpp can build
// Command Reference table.
std::vector<std::string> build_cmd_ref_table();
