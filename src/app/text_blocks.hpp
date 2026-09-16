#pragma once

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

// --- static text for overlay content --- //
/* ascii only */

using TextBlockRef = std::span<const std::string_view>;

inline constexpr auto sh_helpBlock =
    std::to_array<std::string_view> (
        {" apb - a pileup browser                             ",
         "  apb is an terminal-based genome browser designed  ",
         "  for viewing and querying pileup loci.             ",
         "                                                    ",
         "  The browser is navigated with the keyboard.       ",
         "  Usage commands are typed into an in-app command.  ",
         "  line and submitted with Enter.                    ",
         "  Queries are made with a simple SQL-based syntax.  ",
         "                                                    ",
         "  Read the manual for a complete guide to usage,    ",
         "  including query examples. Find it as MANUAL.md    ",
         "  in the repo, or run `apb --manual > MANUAL.md`    ",
         "                                                    ",
         "  For navigation quick reference:                   ",
         "    `? nav`                                         ",
         "  For list of available commands:                   ",
         "    `? cmd`                                         ",
         "                                                    ",
         "  All coordinate data is displayed in 0-indexed     ",
         "  half-open coordinates, matching the internal      ",
         "  representation used by htslib                     "}
    );
static_assert (
    !sh_helpBlock.empty() &&
        std::ranges::all_of (
            sh_helpBlock,
            [] (std::string_view r) {
              return r.size() == sh_helpBlock.front().size();
            }
        ),
    "rows must be of the same width"
);

inline constexpr auto sh_navBlock =
    std::to_array<std::string_view> (
        {" BROWSER PANE                                  ",
         "  Up / Down         scroll one row             ",
         "  PgUp / PgDn       scroll one page            ",
         "                                               ",
         " COMMAND LINE                                  ",
         "  Enter             run command                ",
         "  S-Up / S-Down     step command history       ",
         "  Left / Right      move cursor                ",
         "  M-Left / M-Right  back / forward one word    ",
         "  M-b / M-f         back / forward one word    ",
         "  C-a / C-e         start / end of line        ",
         "  Bksp / M-Bksp     delete char / whole line   ",
         "  C-c               clear input, else quit apb ",
         "                                               ",
         " M-: Alt | C-: Ctrl | S-: Shift                "}
    );
static_assert (
    !sh_navBlock.empty() &&
        std::ranges::all_of (
            sh_navBlock,
            [] (std::string_view r) {
              return r.size() == sh_navBlock.front().size();
            }
        ),
    "rows must be of the same width"
);
