#pragma once

#include <algorithm>
#include <array>
#include <span>
#include <string_view>


namespace helpblocks {

// --- static text for overlay content --- //
/* ascii only */

using TextBlockRef = std::span<const std::string_view>;

inline constexpr auto app = std::to_array<std::string_view> ({
    " apb - a pileup browser                             ",
    "  apb is a terminal-based genome browser designed   ",
    "  for viewing and querying pileup loci.             ",
    "                                                    ",
    "  apb is navigated with the keyboard.               ",
    "  Commands are typed into the in-app command prompt ",
    "  and submitted with Enter.                         ",
    "  Queries are made with a simple SQL-based syntax.  ",
    "                                                    ",
    "  For navigation quick reference:                   ",
    "    `help nav`                                      ",
    "  For list of available commands:                   ",
    "    `help cmd`                                      ",
    "  For list of queryable data:                       ",
    "    `help col`                                      ",
    "                                                    ",
    "  Read the manual for a complete guide to usage,    ",
    "  including query examples - run                    ",
    "    `apb --manual > MANUAL.txt`                     ",
    "  at the CLI to get a copy.                         ",
    "                                                    ",
    "  All coordinate data is displayed in 1-based       ",
    "  closed coordinates.                               ",
});
static_assert (
    !app.empty() && std::ranges::all_of (
                        app,
                        [] (std::string_view r) {
                          return r.size() == app.front().size();
                        }
                    ),
    "rows must be of the same width"
);

// FIXME: move to event.cpp
inline constexpr auto navigation = std::to_array<std::string_view> ({
    " M-: Alt | C-: Ctrl                                 ",
    "                                                    ",
    " ALWAYS ACTIVE:                                     ",
    "  Esc               change focus/mode between the   ",
    "                    read viewer and the prompt      ",
    "  C-c               quit apb                        ",
    "                                                    ",
    " FOCUS: VIEWER                                      ",
    "  Up / Down         scroll one row                  ",
    "  Left / Right      pan alignment pane side-to-side ",
    "  PgUp / PgDn       scroll one page                 ",
    "                                                    ",
    " FOCUS: PROMPT                                      ",
    "  C-w               recall where clause to command  ",
    "                    prompt                          ",
    "  C-o               recall order by clause to       ",
    "                    command prompt                  ",
    "  C-/               undo last modification to       ",
    "                    query                           ",
    "                                                    ",
    "  Enter             submit command                  ",
    "  Left / Right      move cursor                     ",
    "  Up / Down         scroll command history          ",
    "  C-Left / C-Right                                  ",
    "  M-Left / M-Right                                  ",
    "  M-b / M-f         back / forward one word         ",
    "  C-a / C-e                                         ",
    "  Home / End        start / end of line             ",
    "  Bksp / C-d        delete char back / forward      ",
    "  M-Bksp / M-d      delete word back / forward      ",
    "  C-k / C-u         delete to end / start of line   ",
});
static_assert (
    !navigation.empty() && std::ranges::all_of (
                               navigation,
                               [] (std::string_view r) {
                                 return r.size() ==
                                        navigation.front().size();
                               }
                           ),
    "rows must be of the same width"
);

inline constexpr auto tableColumns = std::to_array<std::string_view> ({
    " TABLE REFERENCE                                ",
    "                                                ",
    " The following columns are available for        ",
    " querying. `qname` through `mstart` may be      ",
    " displayed in the table pane with the `col`     ",
    " command.                                       ",
    "                                                ",
    "  `path`:                                       ",
    "      text                                      ",
    "      Path to alignment file.                   ",
    "  `seq`:                                        ",
    "      text / NULL                               ",
    "      the read sequence. NULL if missing.       ",
    "  `qual`:                                       ",
    "      text / NULL                               ",
    "      the read Phred base qualities. NULL if    ",
    "      missing.                                  ",
    "  `base`:                                       ",
    "      character / NULL                          ",
    "      Nucleotide at the pileup position. NULL   ",
    "      if base is deleted, or `seq` is missing.  ",
    "  `indel`:                                      ",
    "      integer                                   ",
    "      indel length to the next mapped           ",
    "      nucleotide in the read.                   ",
    "      (0 = no indel, >0 insertion, <0 deletion) ",
    "  `qname`:                                      ",
    "      text / NULL                               ",
    "      the read/template name.                   ",
    "      NULL if missing.                          ",
    "  `flag`:                                       ",
    "      integer (bitfield)                        ",
    "      SAM bitwise FLAG.                         ",
    "  `start`:                                      ",
    "      integer > 0                               ",
    "      1-based leftmost mapping position of      ",
    "      read.                                     ",
    "  `end`:                                        ",
    "      integer >= start                          ",
    "      1-based rightmost mapping position of     ",
    "      read.                                     ",
    "  `mapq`:                                       ",
    "      integer 0-255                             ",
    "      read mapping quality.                     ",
    "  `basequal`:                                   ",
    "      integer / NULL                            ",
    "      Phred base quality at the pileup          ",
    "      position. NULL if base is deleted, or     ",
    "      `qual` is missing.                        ",
    "  `qpos`:                                       ",
    "      integer > 0                               ",
    "      1-based offset into `seq`/`qual` to       ",
    "      pileup locus position. In deletions, this ",
    "      is the first base after the deletion.     ",
    "  `cigar`:                                      ",
    "      text                                      ",
    "      CIGAR string of alignment operations.     ",
    "  `mtid`:                                       ",
    "      text / NULL                               ",
    "      reference name of the mate/next read.     ",
    "      = if mate is on same contig. Usually = if ",
    "      mate is unmapped. Usually NULL if read    ",
    "      unpaired.                                 ",
    "      (use `flag` to check if mate is unmapped) ",
    "  `mstart`:                                     ",
    "      integer > 0 / NULL                        ",
    "      mate/next read's leftmost mapping         ",
    "      position. Usually identical to `start`    ",
    "      if mate is unmapped. Usually NULL if read ",
    "      unpaired.                                 ",
    "      (use `flag` to check if mate is unmapped) ",
    "  `tags`:                                       ",
    "      text (JSON) / NULL                        ",
    "      aux tags as JSON; able to be individually ",
    "      queried using ->> operator. NULL if tags  ",
    "      are missing.                              ",
    "  `is_del`:                                     ",
    "      integer 1-0 (true/false)                  ",
    "      1 if `base` is a deletion.                ",
    "  `is_head`:                                    ",
    "      integer 1-0 (true/false)                  ",
    "      1 if `qpos` is the read's first aligned   ",
    "      base.                                     ",
    "  `is_tail`:                                    ",
    "      integer 1-0 (true/false)                  ",
    "      1 if `qpos` is the read's last aligned    ",
    "      base.                                     ",
    "  `is_refskip`:                                 ",
    "      integer 1-0 (true/false)                  ",
    "      1 if `qpos` is a reference skip.          ",
});
static_assert (
    !tableColumns.empty() && std::ranges::all_of (
                                 tableColumns,
                                 [] (std::string_view r) {
                                   return r.size() ==
                                          tableColumns.front().size();
                                 }
                             ),
    "rows must be of the same width"
);

}  // namespace helpblocks
