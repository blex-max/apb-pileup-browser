#include "text_blocks.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#define SIZE_ASSERT_FAIL "rows must be of the same width"

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
     "  C-c               clear input, else quit apb ",
     "                                               ",
     " M-: Alt | C-: Ctrl | S-: Shift                "}
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
     "  `pane [seq|data]`:                           ",
     "    fold/unfold the sequence or data pane,     ",
     "    reset both to default with no args         ",
     "  `help [nav|cmd|<cmd>]` (`?`):                ",
     "    show this reference, navigation help,      ",
     "    or a specific command usage                ",
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


constexpr std::string_view README_MARKDOWN = R"md(
# `apb` - A Pileup Browser

`apb` is a terminal genome browser tailored to exploratory viewing and querying of pileups of mapped reads at genomic loci.
It features an SQL-based command line for querying reads at the selected locus,
coupled with an alignment display showing reads aligned to their genomic positions,
and their divergence from a reference genome. It is chiefly designed for verification and investigation of variant calls, but can be used to inspect the reads at any loci.
The built-in command line is capable of highly complex queries, but is tuned to make exploratory pattern hunting quick and seamless.

Advantages:
- Immediately available in the terminal; no spinning up a genome browser instance or navigating a web UI.
- Easily installed, including on compute cluster nodes.
- Fast; no network IO, responsive UI.
- UI optimised for one job — inspecting pileup loci — rather than general-purpose genome browsing.
- Powerful SQL-backed query syntax for fast exploration.

This software is in a demo state and feedback is very much appreciated as I work towards a 1.0 release!

Below is a text screencap of the TUI, with explanatory comments in CAPTIALS. Rendering is richer and better-looking in the TUI, but the screencap gives the basic idea. Bases which match the provided reference are displayed as `=`, and deletions are displayed as a bold `-`.
```
     READS ALIGNED TO REFERENCE ↓                              PER READ DATA ↓ 
╭─────────────────────────────────────────────────────┬────────────────────────────────────────────────────────────────────────────────╮
│TACGTACGTACGTACGTACGTACGTAAGTACGTACGTACGTACGTACGTACGT│  basequal  │  rstart  │   rend   │ flag │   cigar   │        qname        │    │
├──────────────────────────|──────────────────────────│────────────────────────────────────────────────────────────────────────────────┤
│==========================T===                       │33          │6         │153       │0     │2S147M     │read74               │    │
│==========================T=====                     │26          │6         │155       │0     │149M       │read91               │    │
│==========================T======                    │33          │7         │156       │0     │149M       │read44               │    │
│==========================T===                       │27          │15        │153       │0     │11S138M    │read9                │    │
│==========================T====================      │22          │21        │170       │0     │149M       │read6                │    │
│==========================T==========================│28          │29        │178       │0     │149M       │read40               │    │
│==========================T==========================│22          │36        │185       │0     │149M       │read93               │    │
│==========================T==========================│34          │37        │186       │0     │149M       │read62               │    │
│==========================T==========================│33          │46        │193       │0     │147M2S     │read8                │    │
│====================G=====T=======--=================│39          │51        │202       │0     │106M2D43M  │read48               │    │
│==========================T===G======================│20          │59        │208       │0     │149M       │read26               │    │
│==========================T==========================│30          │67        │216       │0     │149M       │read84               │    │
│==========================T==========================│34          │79        │228       │0     │149M       │read68               │    │
│==========================T==========================│32          │86        │239       │0     │115M4D34M  │read97               │    │
│==========================T==========================│28          │88        │230       │0     │7S142M     │read81               │    │
│==========================T==========================│37          │88        │237       │0     │149M       │read78               │    │
│==========================T==============C===========│22          │93        │229       │0     │136M13S    │read0                │    │
│==========================T============C=============│39          │119       │268       │0     │149M       │read43               │    │
│   =======================T==========================│25          │126       │275       │0     │149M       │read94               │    │
│       ===T===============T==========================│31          │130       │279       │0     │149M       │read3                │    │
│               ===========T==========================│28          │138       │287       │0     │149M       │read7                │    │
│                                                     │                                                                                │
├─────────────────────────────────────────────────────┴────────────────────────────────────────────────────────────────────────────────┤
│ LOCUS: demo:149 │ SPAN: 2-296 │   ← LOCUS INFO                                                                                       │
╭━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╮
│WHERE base != 'A'        ← ACTIVE QUERY                                                                                               │
│──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────│
│:and (flag & 3584) = 0   ← COMMAND LINE                                                                                               │
│──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────│
│OK!                      ← RETURN MESSAGES                                                                                            │
╰                                                                                                                                      ╯
```


## Install

You will need a terminal emulator with basic unicode support. I expect the TUI should render successfully on almost any modern-ish emulator. If you have issues with rendering please report them. `apb` has been confirmed to work in iTerm2, ghostty, vscode, and tmux.

Docker images are provided via the repo GitHub; check the `packages` tab to pull the latest release with docker, singularity, etc. This is the easiest way to get `apb`.

### Building from source

Requires:
- a C++23 compiler (gcc ≥ 12 and clang ≥ 21 are both known to work)
- CMake ≥ 3.22
- `pkg-config`
- `sqlite3` ≥ 3.38
- `htslib` ≥ 1.17

`sqlite3` and `htslib` need to be discoverable via `pkg-config`. If htslib isn't packaged that way on your system, you can point the build process at it directly with `-DHTSLIB_INCLUDE_DIR` and `-DHTSLIB_LIBRARY`. Other dependencies (termbox2, plog, argparse, fmt) are pulled automatically via CMake FetchContent. This means the first build needs network access.

```sh
cmake -S . -B build
cmake --build build
```

The compiled binary can be found at `build/apb`.

## Overview

Given an alignment file and a genomic locus `apb` builds the pileup at that single position and loads the reads into a fast, queryable database structure - one row per read. The TUI then renders the reads as aligned to a reference genome (if provided), displays user-selected data for each read (e.g. mapping quality, leftmost alignment position, etc.) and provides a command line at which you can enter commands to query the reads or change the display. The display is navigated using simple arrow-key navigation.

## CLI Usage

There are three modal subcommands available when starting `apb`.

The CLI is in a demo state - the subcommand approach might not be long term.

`apb sam <alignment-file> <locus> [--ref reference.fasta] [--dump out.db]`  

`apb db <dumped.db>`  

`apb demo [--dump out.db]`  

`sam` opens a live alignment file (SAM/BAM/CRAM) at a locus (`chr1:12345`) and launches the TUI. When specifying a locus, it is in the form `contig:coordinate` - only a single coordinate needs to be provided, rather than a length-1 range as in many `samtools` commands. **The locus coordinate is 1-based**, as `samtools` CLI commands.

`db` reopens a database file previously produced by `--dump` (or the in-TUI `dump` command).

`demo` runs against synthetic data, no alignment file required. Good for a first look at the tool, but note that since the data is artifically generated not everything works quite as it should - some fields are not properly set in the database.

`apb --log <path.txt>` (`--log` comes before the subcommand) enables debug logging. Valuable to turn on during this early development stage in case any crashes are encountered!

## TUI Usage

Note that this is all subject to change pending user feedback.

### Basic Navigation

Normal typing goes directly to the command line. `Enter` dispatches the contents as a command.

**Navigation Keys**:

**Browser pane**
- `Shift+↑` / `Shift+↓` scroll the alignment view by one row.
- `PgUp` / `PgDn` scroll by a full page.

**Command line**
- `↑` / `↓` step through command history.
- `←` / `→` move the cursor; `Ctrl-A` / `Ctrl-E` jump to start/end; `Alt+←` / `Alt+→` (or `Alt+b` / `Alt+f`) jump by word.
- `Backspace` deletes a character; `Alt+Backspace` clears the whole line.

`Ctrl-C` clears the command line if any input is present, and exits the program otherwise.

### Command Reference

| Command | Aliases | Args | Effect |
|---|---|---|---|
| `show` | | `<field>...` | Add columns to the display |
| `hide` | | `<field>...` | Remove columns from the display |
| `where` | `w` | `<clause>` | Start a new WHERE clause |
| `and` | | `<clause>` | Extend the active WHERE clause with AND |
| `or` | | `<clause>` | Extend the active WHERE clause with OR |
| `back` | | | Undo the last `where`/`and`/`or` |
| `order` | `o` | `<clause>` | Set the ORDER BY clause |
| `clear-where` | `cw` | | Clear the WHERE clause of the active query, retaining ORDER BY |
| `clear` | | | Clear the active query |
| `count` | | `[clause]` | Count matching reads without touching the active query (Any clause argument is AND-concatenated to the existing query) |
| `dump` | | `<path>` | Write the current in-memory database to a sqlite3 file on disk |
| `pane` | | `[seq\|data]` | Fold/unfold the sequence or data pane; reset both to default with no args |
| `readme` | | `[path]` | Write the readme to `[path]`, or the working directory if omitted |
| `help` | `?` | `[nav\|cmd\|<cmd>]` | Show this reference, navigation help, or a specific command's usage |
| `quit` | `q` | | Exit |

Every line typed at the command line is dispatched like `<command> [args]`.

### Querying the Pileup

The `apb` query commands provide a simple wrapper around (SQLite-flavoured) SQL. Whereas a more generic SQL REPL might be structured around dispatching known, predetermined queries, `apb` aims to support stepwise pattern discovery. The active query (a WHERE clause plus an ORDER BY) persists across commands and may be extended a piece at a time (using `where`, `and`/`or`, and `order`). By example, a session might look like: check which reads carry a non-reference base, filter those by a base quality threshold, decide that's not informative and back up, filter by mapping quality instead, and so on. See [Table Reference](#Table-Reference) for the full list of queryable columns.

#### Query Walkthrough

Imagine a putative variant locus under manual inspection. Starting broad, we can filter the view to show only reads with a non-reference base at the pileup position:
```
where base != 'G'
```
We then filter reads with any of flag bits 9/10/11 set (supplementary, duplicate, or QC fail):
```
and (flag & 3584) = 0
```
Note that flag is a bitmask - see [here](https://www.w3schools.com/programming/prog_operators_bitwise.php) for a quick introduction to bitmask syntax. SQLite supports the bitwise and, or, xor and not. [This webpage](https://broadinstitute.github.io/picard/explain-flags.html) from the Broad Institute is very useful for finding the corresponding integer given a set of SAM bits.

Back to our query. Finally, let's sort to see the weakest evidence first:
```
order basequal ASC
```
ASC is simply SQLite's shorthand for ascending. DESC is the alternative. If you omit the sort direction term, ASC is the default. Also note that you can order by multiple keys, e.g. `order basequal DESC, rstart ASC` would sort by basequal in descending order, and where basequal is equal, alignment start position will be used as a secondary key.

In total, three REPL commands, each a plain SQL fragment — an inequality, a bitwise flag check, a column to sort by. The parser wraps these commands into a complete SQL statement. Note that you could also write the full command as a single statment:
```
where base != 'G' AND (flag & 3584) = 0 ORDER BY basequal ASC
```
The two styles are equally supported; in both cases, you can continue to add on further clauses with `and` and `or` as you like. Or remove them with `back`!

If you want a **count** rather than a filtered view, `count [clause]` answers without disturbing the active query. For example, `count mapq < 20` tells you how many low-mapping-quality reads are without chainging the view.

#### Further Examples

Beyond plain comparisons, SQLite's full function library is available. A few examples:

##### Motifs at the query position

`qpos` is the 0-based offset into `seq` for the base at the pileup position, and SQLite's `substr()` is 1-based, so to search for the 4-mer `GATC` starting at the query position:
```
where substr(seq, qpos + 1, 4) = 'GATC'
```
`LIKE` (`_`/`%`) and `GLOB` (`?`/`*`/`[ACG]`) both work as wildcards — `GLOB`'s character classes are useful for ambiguity. To search for two possible trinucleotide motifs at the query position:
```
where substr(seq, qpos + 1, 3) glob 'A[CG]T'
```
You can also search for motifs within a window of the `seq` string. This command searches for `GATC` within the first 10 bases of the read:
```
where instr(substr(seq, 1, 10), 'GATC') > 0
```

##### Aux tags

`tags` is a JSON blob of the read's aux tags — extract tags with `->>`:
```
where tags ->> '$.NM' > 2
```
or checking a read group:
```
where tags ->> '$.RG' = 'sample1'
```
A read with no aux tags, or missing that specific tag, comes back as SQL `NULL` rather than an error, so `where tags ->> '$.RG' is null` finds reads missing that tag.

##### Cigar querying

`cigar` is a plain string, so text matching works directly on it. E.g. to search for soft-clipped reads:
```
where cigar like '%S%'
```

More is possible. See [SQLite's expression/function reference](https://sqlite.org/lang_expr.html).

#### Table Reference

For each read, the database stores the following information. All columns are queryable in `where`/`and`/`or`/`order` commands. If the content of a column is not clearly displayed by the alignment view, the column can be displayed alongside the reads in tabular format.

| Column | Meaning |
|---|---|
| `qname` | read/template name |
| `flag` | SAM bitwise FLAG |
| `rstart` | 0-based leftmost mapping position |
| `rend` | 0-based rightmost mapping position |
| `mapq` | mapping quality |
| `base` | the read's base at the pileup position |
| `basequal` | Phred base quality at the pileup position |
| `qpos` | 0-based offset into `seq`/`qual` for the pileup locus position |
| `cigar` | CIGAR string |
| `mtid` | reference name of the mate/next read |
| `mstart` | mate/next read's leftmost mapping position |
| `tags` | aux tags as JSON; able to be individually queried |
| `indel` | indel length to the next mapped base in the read (0 none, >0 insertion, <0 deletion) |
| `is_del` | 1 if this position is a deletion |
| `is_head` | 1 if this is the read's first aligned base |
| `is_tail` | 1 if this is the read's last aligned base |
| `is_refskip` | 1 if this position is a reference skip |
| `seq` | the read's sequence string |
| `qual` | the read's ASCII quality string |
| `ncig` | number of CIGAR operations in the read |

`indel` might require some explanation. Essentially, if the base at the pileup position is followed by an indel, then `indel` will contain the size of that indel event. A deletion is represented by a negative size (bases lost), and an insertion is represented by a positive size (bases gained). I need to confirm the behaviour of the field when the pileup base itself is deleted.

The first twelve (`qname` through `tags`) can also be displayed in the info pane with `show`/`hide` commands.

For advanced users: most of these map directly onto fields in htslib's `bam_pileup1_t` and `bam1_t` structs, if you want to cross-reference against htslib's own documentation.

### A Word on `dump` Functionality

A dump is a small, self-contained sqlite3 file with just the reads at this one locus. Picking a session back up later with `apb db` is one reason to use it; a few others:

- Full SQL — `sqlite3 my.db` gets you everything the in-TUI REPL deliberately doesn't: `GROUP BY`, aggregates, etc. Allows for more complex analysis if needed.
- Downstream use — it's a normal sqlite3 file, so anything with a sqlite driver can read it.
- Sharing — send a colleague exactly the reads you're looking at, at a fraction of the size, without them needing the original BAM/CRAM, reference genome, or even `apb` if they're happy just to use `sqlite3`.
- Debugging (for developers) — a stable snapshot of exactly what got loaded, inspectable without the original alignment file or the TUI. Mostly relevant if you're developing `apb` itself, rather than just using it.

### A Word on Indexing Systems

`htslib`/`samtools`/`bcftools`, and by extension all alignment and VCF data, mix 3 (3!!) coordinate systems. This can be tricky to navigate.

`apb` uses 0-based half-open coordinates throughout, **except for the locus argument when starting `apb` from the command line, which is 1-based**. A 1-based locus argument has the advantage of being identical to the VCF `POS` field per the VCF specification, and to `samtools` commands e.g. `samtools view ...`. However, `htslib`'s internal alignment representation format is 0-based, so it is more natural (and less bug-prone) to display the alignment information as 0-based. This is an inevitable UX compromise - feedback is appreciated.

## Future Roadmap

Feature suggestions are welcomed.

### Planned
- VCF-driven locus browsing - input a VCF along with alignment/s and navigate between variant loci.
  - unlikely to implement any filtering of the vcf as that can be done at or before startup with `bcftools` and shell piping/substitution.
- Column discoverability (e.g. an in-app column reference).
- Clear indication of no-op navigation via blinking the staus bar or similar
- Indication of insertion sites by gapping the reference/other reads.
- Pannable alignment view (currently the view is only scrollable up/down - side to side is planned).
- Fold-out display of quality string below each aligned read.
- Minor UX/UI improvments.
- Headless `count` mode, to get results for a query known at the CLI without dropping into the TUI.
- More stats in the status bar; allele counts, VAF (when in variant driven mode), reference span complexity assessment (useful when assessing artefactual variants).

### Speculative
These are items that I think might be useful,
but are more work so I will only add them if users
find them desirable.

- Multiple alignment pileups.
- Locus-jumping from within TUI when reading an alignment file - e.g. `goto chr1:2500`.
  - Currently the view is fixed to a single locus specified at startup.
  - This may also lead to multi-locus dbs, multi-sample browsing/dbs, etc.


## Development

### Dependencies

| Dependency | Version | Found via | Used for |
|---|---|---|---|
| sqlite3 | ≥3.38 | system, `pkg-config` | query/storage layer |
| htslib | ≥1.17 | system, `pkg-config` (or `-DHTSLIB_INCLUDE_DIR`/`-DHTSLIB_LIBRARY`) | Handling sequence data |
| termbox2 | 605398fa | CMake FetchContent | terminal rendering and raw input events |
| plog | v1.1.10 | CMake FetchContent | debug logging |
| argparse | v3.2 | CMake FetchContent | CLI |
| fmt | v12.2.0 | CMake FetchContent | string formatting |
| Catch2 [optional] | v3.8.1 | CMake FetchContent | test framework |

### Tests

Test files live in `tests/`. Build and run them with:
```sh
cmake -S . -B build -DMAKE_TEST=ON
cmake --build build -j
ctest --test-dir build
```
Coverage is concentrated on the backend. TUI rendering and event handling aren't unit tested at this time.

### AI Usage Policy

I think it's important to be up front about AI usage. This repo has been developed by hand, with use of AI as a second line — for bouncing ideas off of, bug hunting, and basic stub implementation. Architecture, the design of all core primitives and functions, and other impactful decisions are made by the maintainer. Small, mechanical, additive changes (a keybinding, a warning fix, a rename) might be handed over. A new feature or refactor is not; those are designed and implemented manually. The benefit is a codebase that is (hopefully) well-designed, effective, and concise - and therefore easy to maintain and easy to contribute to (pending some alpha cleanup). Contributions are more than welcome, but would ideally follow this standard.
)md";

std::string_view get_readme() { return README_MARKDOWN; }

constexpr std::string_view CLI_INTRO =
    R"txt( apb is an terminal-based genome browser designed for viewing
 and querying pileup loci. It features a REPL-like command
 line and simple SQL-based query syntax.)txt";

std::string_view get_cli_intro() { return CLI_INTRO; }

constexpr std::string_view CLI_EPILOG =
    R"txt( See README.md for further info, or use the in-app help
 (type ? and press enter in the TUI).If you don't have
 the readme, it can be written to disk from the TUI
 using the `readme` command.

 In the TUI, type q and press enter or press Ctrl-C
 twice to quit.)txt";

std::string_view get_cli_epilog() { return CLI_EPILOG; }
