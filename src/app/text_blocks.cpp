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

// TODO: add fold cmds
// NOTE: is it possible to generate this off
// the cmd structs .usage?
constexpr auto CMD_BLOCK = std::to_array<std::string_view> (
    {" COMMAND REFERENCE                             ",
     "  `readme [path]`:                             ",
     "    dump complete readme to [path], or cwd if  ",
     "    path is omitted.                           ",
     "  `where <clause>`:                            ",
     "    start a new query                          ",
     "  `and <clause>`:                              ",
     "    and-append a clause onto an existing       ",
     "    query.                                     ",
     "  `or <clause>`:                               ",
     "    or-append a clause onto an existing        ",
     "    query.                                     ",
     "  `back`:                                      ",
     "    undo the last where/and/or                 ",
     "  `order <clause>`:                            ",
     "    set the active query's sort order          ",
     "  `clear-where`:                               ",
     "    clear the where clause, keeping the        ",
     "    order clause                               ",
     "  `clear`:                                     ",
     "    clear the whole active query               ",
     "  `count [clause]`:                            ",
     "    count matches without touching the         ",
     "    active query. [clause] is and-appended     ",
     "    to the existing query, if given.           ",
     "  `show <field>...`:                           ",
     "    add field(s) to the display                ",
     "  `hide <field>...`:                           ",
     "    remove field(s) from the display           ",
     "  `dump <path>`:                               ",
     "    write the in-memory database to an         ",
     "    sqlite3 file                               ",
     "  `quit`:                                      ",
     "    exit apb                                   "}
);
static_assert (
    !CMD_BLOCK.empty() && std::ranges::all_of (
                              CMD_BLOCK,
                              [] (std::string_view r) {
                                return r.size() ==
                                       CMD_BLOCK.front().size();
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
    case TxtBlockId::cmdRef:
      return CMD_BLOCK;
    default:
      return {};
  }
}
