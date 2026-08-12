#include "text_blocks.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#define SIZE_ASSERT_FAIL "rows must be of the same width"

// apb is an terminal-based genome browser designed for viewing
//  and querying pileup loci. It features a REPL-like command
//  line and simple SQL-based query syntax.
constexpr auto HELP_BLOCK = std::to_array<std::string_view> (
    {" apb - a pileup browser                             ",
     "  apb is an terminal-based genome browser designed  ",
     "  for viewing and querying pileup loci. It features ",
     "  a REPL-like command line and simple SQL-based     ",
     "  query syntax.                                     ",
     "                                                    ",
     "  The browser is navigated with the keyboard.       ",
     "  Commands are typed and submitted with Enter.      ",
     "                                                    ",
     "  Read the readme for a complete guide to usage,    ",
     "  including query examples. The readme can be       ",
     "  written to disk from within apb using the command ",
     "  `readme <path>`                                   ",
     "                                                    ",
     "  For navigation quick reference:                   ",
     "    `? nav`                                         ",
     "  For list of available commands:                   ",
     "    `? cmd`                                         "}
);
static_assert (
    !HELP_BLOCK.empty() &&
        std::ranges::all_of (
            HELP_BLOCK,
            [] (std::string_view r) {
              return r.size() == HELP_BLOCK.front().size();
            }
        ),
    SIZE_ASSERT_FAIL
);

constexpr auto NAV_BLOCK = std::to_array<std::string_view> (
    {" BROWSER PANE                                  ",
     "  S-Up / S-Down     scroll one row             ",
     "  PgUp / PgDn       scroll one page            ",
     "                                               ",
     " COMMAND LINE                                  ",
     "  Enter             run command                ",
     "  Up / Down         step command history       ",
     "  Left / Right      move cursor                ",
     "  M-Left / M-Right  back / forward one word    ",
     "  M-b / M-f         back / forward one word    ",
     "  C-a / C-e         start / end of line        ",
     "  Bksp / M-Bksp     delete char / whole line   ",
     "  C-c               clear input, else quit apb "}
);
static_assert (
    !NAV_BLOCK.empty() && std::ranges::all_of (
                              NAV_BLOCK,
                              [] (std::string_view r) {
                                return r.size() ==
                                       NAV_BLOCK.front().size();
                              }
                          ),
    SIZE_ASSERT_FAIL
);


TextBlockRef get_text_block (TxtBlockId id)
{
  switch (id) {
    case TxtBlockId::generalHelp:
      return HELP_BLOCK;
    case TxtBlockId::navHelp:
      return NAV_BLOCK;
  }
}
