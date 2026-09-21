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
         "  For navigation quick reference:                   ",
         "    `? nav`                                         ",
         "  For list of available commands:                   ",
         "    `? cmd`                                         ",
         "  For list of queryable data:                       ",
         "    `? table`                                       ",
         "                                                    ",
         "  The browser is navigated with the keyboard.       ",
         "  Usage commands are typed into an in-app command.  ",
         "  line and submitted with Enter.                    ",
         "  Queries are made with a simple SQL-based syntax.  ",
         "                                                    ",
         "  Read the manual for a complete guide to usage,    ",
         "  including query examples. Find it as MANUAL.txt   ",
         "  in the repo, or run `apb --manual > MANUAL.txt`.  ",
         "                                                    ",
         "  All coordinate data is displayed in 0-indexed     ",
         "  half-open coordinates, matching the internal      ",
         "  representation used by htslib.                    "}
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

inline constexpr auto sh_colBlock =
    std::to_array<std::string_view> ({
        " TABLE REFERENCE                                ",
        "                                                ",
        " All of the following columns may be referenced ",
        " in query commands, e.g. `where`. The first 11  ",
        " may be displayed in the table pane with the    ",
        " `col` command.                                 ",
        "                                                ",
        "  `qname`:                                      ",
        "      read/template name                        ",
        "  `flag`:                                       ",
        "      SAM bitwise FLAG                          ",
        "  `rstart`:                                     ",
        "      0-based leftmost mapping position         ",
        "  `rend`:                                       ",
        "      0-based rightmost mapping position        ",
        "  `mapq`:                                       ",
        "      mapping quality                           ",
        "  `basequal`:                                   ",
        "      Phred base quality at the pileup position ",
        "  `qpos`:                                       ",
        "      0-based offset into `seq`/`qual` to reach ",
        "      pileup locus position                     ",
        "  `cigar`:                                      ",
        "      CIGAR string                              ",
        "  `mtid`:                                       ",
        "      reference name of the mate/next read      ",
        "  `mstart`:                                     ",
        "      mate/next read's leftmost mapping         ",
        "      position                                  ",
        "  `tags`:                                       ",
        "      aux tags as JSON; able to be individually ",
        "      queried                                   ",
        "  `base`:                                       ",
        "      the read's base at the pileup position    ",
        "  `indel`:                                      ",
        "      indel length to the next mapped base in   ",
        "      the read (0 none, >0 insertion,           ",
        "      <0 deletion)                              ",
        "  `is_del`:                                     ",
        "      1 if this position is a deletion          ",
        "  `is_head`:                                    ",
        "      1 if this is the read's first aligned     ",
        "      base                                      ",
        "  `is_tail`:                                    ",
        "      1 if this is the read's last aligned base ",
        "  `is_refskip`:                                 ",
        "      1 if this position is a reference skip    ",
        "  `seq`:                                        ",
        "      the read sequence                         ",
        "  `qual`:                                       ",
        "      the read quality string in ASCII format   ",
        "  `ncig`:                                       ",
        "      number of CIGAR operations in the read    ",
    });
static_assert (
    !sh_colBlock.empty() &&
        std::ranges::all_of (
            sh_colBlock,
            [] (std::string_view r) {
              return r.size() == sh_colBlock.front().size();
            }
        ),
    "rows must be of the same width"
);
