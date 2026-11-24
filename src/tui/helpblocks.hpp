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
    "  apb is navigated with the keyboard. There are two ",
    "  modes, `viewer` and `prompt`. When the read       ",
    "  viewer is focused, the reads can be browsed with  ",
    "  the arrow keys. When the prompt is focused,       ",
    "  commands are typed and edited at the prompt       ",
    "  and submitted with Enter. Switch mode with `Esc`. ",
    "                                                    ",
    "  The reads may be queried via the prompt. Queries  ",
    "  are written in a simple SQL-based syntax.         ",
    "  Read the manual for a complete guide to usage,    ",
    "  including querying - run                          ",
    "    `apb --manual > MANUAL.md`                      ",
    "  at the CLI to get a copy on disk, or check the    ",
    "  GitHub repo where an online manual is hosted:     ",
    "  https://blex-max.github.io/apb-pileup-browser/    ",
    "                                                    ",
    "  For navigation quick reference:                   ",
    "    `help nav`                                      ",
    "  For list of available commands:                   ",
    "    `help cmd`                                      ",
    "  For list of queryable data:                       ",
    "    `help col`                                      ",
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

// FIXME: fix ambiguous "no pileup" wording
// FIXME: fix misleading NULLs for apb-produced
// databases
inline constexpr auto tableColumns = std::to_array<std::string_view> ({
    " TABLE REFERENCE                                ",
    "                                                ",
    " The following columns are available for        ",
    " querying. `qname` through `mstart` may be      ",
    " displayed in the table pane with the `col`     ",
    " command.                                       ",
    "                                                ",
    "  `source`:                                     ",
    "      text                                      ",
    "      Record source, e.g. alignment file path.  ",
    "  `seq`:                                        ",
    "      text / NULL                               ",
    "      the read sequence. NULL if missing.       ",
    "  `qual`:                                       ",
    "      text / NULL                               ",
    "      the read base qualities as Phred+33       ",
    "      ASCII. NULL if missing.                   ",
    "  `base`:                                       ",
    "      character / NULL                          ",
    "      Nucleotide at the pileup position. NULL   ",
    "      if base is deleted or a reference skip,   ",
    "      `seq` is missing, or no pileup.           ",
    "  `indel`:                                      ",
    "      integer / NULL                            ",
    "      indel length to the next mapped           ",
    "      nucleotide in the read. NULL if no pileup.",
    "      (0 = no indel, >0 insertion, <0 deletion) ",
    "  `qname`:                                      ",
    "      text / NULL                               ",
    "      the read/template name.                   ",
    "      NULL if missing.                          ",
    "  `flag`:                                       ",
    "      integer (bitfield) / NULL                 ",
    "      SAM bitwise FLAG. NULL if missing.        ",
    "  `start`:                                      ",
    "      integer > 0                               ",
    "      1-based leftmost mapping position of      ",
    "      read.                                     ",
    "  `end`:                                        ",
    "      integer >= start / NULL                   ",
    "      1-based rightmost mapping position of     ",
    "      read. NULL if missing.                    ",
    "  `mapq`:                                       ",
    "      integer 0-254 / NULL                      ",
    "      read mapping quality. NULL if             ",
    "      unavailable (255 in SAM).                 ",
    "  `basequal`:                                   ",
    "      integer / NULL                            ",
    "      Phred base quality at the pileup          ",
    "      position. NULL if base is deleted or a    ",
    "      reference skip, `qual` is missing, or no  ",
    "      pileup.                                   ",
    "  `qpos`:                                       ",
    "      integer > 0 / NULL                        ",
    "      1-based offset into `seq`/`qual` to       ",
    "      pileup locus position. In deletions, this ",
    "      is the first base after the deletion.     ",
    "      NULL if no pileup.                        ",
    "  `cigar`:                                      ",
    "      text / NULL                               ",
    "      CIGAR string of alignment operations.     ",
    "      NULL if missing.                          ",
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
    "      integer 1-0 (true/false) / NULL           ",
    "      1 if `base` is a deletion. NULL if no     ",
    "      pileup.                                   ",
    "  `is_refskip`:                                 ",
    "      integer 1-0 (true/false) / NULL           ",
    "      1 if `qpos` is a reference skip. NULL if  ",
    "      no pileup.                                ",
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
