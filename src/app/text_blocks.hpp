#pragma once

#include <cstddef>
#include <span>
#include <string_view>

// --- static text for overlay content --- //
/* ascii only */

using TextBlockRef = std::span<const std::string_view>;

enum class TxtBlockId : uint8_t { generalHelp, navHelp, cmdRef };
TextBlockRef get_text_block (TxtBlockId id);


std::string_view get_readme();

std::string_view get_cli_intro();

std::string_view get_cli_epilog();
