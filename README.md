# `apb` - A Pileup Browser

`apb` is a terminal genome browser tailored to exploratory viewing and querying of pileups of mapped reads at genomic loci.
It features an SQL-based command line for querying reads at the selected locus,
coupled with an alignment display showing reads aligned to their genomic positions,
and their divergence from a reference genome. It is chiefly designed for verification and investigation of variant calls, but can be used to inspect the reads at any loci.
The built-in command line is capable of highly complex queries, but is tuned to make exploratory pattern hunting quick and seamless.

Advantages:
- Immediately available in the terminal; no spinning up a genome browser instance or navigating a web UI.
- Easily installed, anywhere including on compute cluster nodes.
- Fast; no network IO, responsive UI.
- UI optimised for one job — inspecting pileup loci — rather than general-purpose genome browsing.
- A real, powerful SQL-backed query syntax for fast exploration.

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

Requires a C++23 compiler, CMake ≥3.22, and `pkg-config`. `sqlite3` (≥3.38) and `htslib` (≥1.17) need to be discoverable via `pkg-config`; if htslib isn't packaged that way on your system, point the build process at it directly with `-DHTSLIB_INCLUDE_DIR` and `-DHTSLIB_LIBRARY`. Everything else (termbox2, plog, argparse, fmt) is pulled automatically via CMake FetchContent, so the first configure needs network access.

```sh
cmake -S . -B build
cmake --build build
```

The compiled binary can be found at `build/apb`.

## Overview

Given an alignment file and a genomic locus `apb` builds the pileup at that single position and loads the reads into a fast, queryable database structure - one row per read. The TUI then renders the reads as aligned to a reference genome (if provided), displays user-selected data for each read (e.g. mapping quality, leftmost alignment position, etc.) and provides a command line at which you can enter commands to query the reads or change the display. The display is navigated using simple arrow-key navigation.

## CLI Usage

There are three modal subcommands available when starting `apb` in the terminal.

The CLI is in a demo state - the subcommand approach might not be long term.

`apb sam <alignment-file> <locus> [--ref reference.fasta] [--dump out.db]`  

`apb db <dumped.db>`  

`apb demo [--dump out.db]`  

`sam` opens a live alignment file (SAM/BAM/CRAM) at a locus (`chr1:12345`) and launches the TUI. `--dump` skips the TUI and writes the resulting database straight to disk instead — useful for headless/batch use. When specifying a locus, it is in the form `contig:coordinate` - only a single coordinate needs to be provided, rather than a length-1 range as in many `samtools` commands. **The locus coordinate is 1-based**.

`db` reopens a database file previously produced by `--dump` (or the in-TUI `dump` command).

`demo` runs against synthetic data, no alignment file required — good for a first look at the tool.

`apb --log <path.txt>` (`--log` comes before the subcommand) enables debug logging. Valuable to turn on during this early development stage in case any crashes are encountered!

## TUI Usage

Note that this is all subject to change pending user feedback.

### Basic Navigation

Normal typing goes directly to the command line; `Enter` dispatches it as a command (see [The Command Line](#the-command-line) below). Navigation keys navigate the alignment view, and the command line.

**Browser pane**
- `Shift+↑` / `Shift+↓` scroll the alignment view by one row.
- `PgUp` / `PgDn` scroll by a full page.

**Command line**
- `↑` / `↓` step through command history.
- `←` / `→` move the cursor; `Ctrl-A` / `Ctrl-E` jump to start/end; `Alt+←` / `Alt+→` (or `Alt+b` / `Alt+f`) jump by word.
- `Backspace` deletes a character; `Alt+Backspace` clears the whole line.

### TUI Commands

| Command | Aliases | Args | Effect |
|---|---|---|---|
| `show` | | `<field>...` | Add columns to the display |
| `hide` | | `<field>...` | Remove columns from the display |
| `where` | `w` | `<clause>` | Start a new WHERE clause (fails if one's already active — `clear`/`clear-where` first) |
| `and` | | `<clause>` | Extend the active WHERE clause with AND |
| `or` | | `<clause>` | Extend the active WHERE clause with OR |
| `back` | | | Undo the last `where`/`and`/`or` |
| `order` | `o` | `<clause>` | Set the ORDER BY clause |
| `clear-where` | `cw` | | Drop the WHERE clause, keep ORDER BY |
| `clear` | | | Reset the whole query (WHERE and ORDER BY) |
| `count` | | `[clause]` | Count matching reads without touching the active query (Any clause argument is AND-concatenated to the existing query) |
| `dump` | | `<path>` | Write the current in-memory database to a sqlite3 file on disk |
| `quit` | `q` | | Exit |

Every line typed at the command line is dispatched like `<command> [args]`. The active query — a WHERE clause plus an ORDER BY — persists across commands and is extended a piece at a time (`where`, then `and`/`or`, then `order`) rather than retyped from scratch each time.

Filtering and sorting uses simple boolean conditions on the columns: `where mapq >= 30` keeps only confidently-mapped reads; `where base = 'A' and flag & 16 = 0` narrows to forward-strand reads with an A at the query position. No deep SQL knowledge is needed for this — it's just comparisons (`=`, `!=`, `<`, `>`) joined with `and`/`or`. FLAG is a bitmask, so `&` is how that example picks out bit 5 (`16`, reverse-strand) — `flag & 4` (bit 2) similarly isolates "unmapped" reads, and any number of bits can be combined into one expression. It all compiles down to a real SQL `WHERE` clause, which lets things scale up to genuinely complex queries later if you want them (more in Query Syntax in Depth, below).


### Querying the Pileup

`apb` is built for exploration. Whereas more generic SQL interface programs are structured around dispatching known, predetermined queries, `apb` aims to support a stepwise pattern discovery. By example, a session might look like: check which reads carry a non-reference base, filter those by a base quality threshold, decide that's not informative and back up, filter by mapping quality instead, and so on. 

#### The `reads` Table

For each read, the database stores the following information. All of these may be queried at the command line. Each column can be displayed alongside the reads unless the information is clearly displayed by the alignment view.

| Column | Meaning |
|---|---|
| `qname` | read/template name (QNAME) |
| `flag` | SAM bitwise FLAG |
| `rstart` | 0-based leftmost mapping position |
| `rend` | 0-based rightmost mapping position |
| `mapq` | mapping quality |
| `base` | the read's base at the pileup position |
| `basequal` | base quality (Phred) at the pileup position |
| `qpos` | 0-based offset into `seq`/`qual` for this position |
| `cigar` | full CIGAR string |
| `mtid` | reference name of the mate/next read |
| `mstart` | mate/next read's leftmost mapping position |
| `tags` | aux tags as JSON — able to be individually queried |
| `indel` | indel length to the next position (0 none, >0 insertion, <0 deletion) |
| `is_del` | 1 if this position is a deletion |
| `is_head` | 1 if this is the read's first aligned base |
| `is_tail` | 1 if this is the read's last aligned base |
| `is_refskip` | 1 if this position is a reference skip (e.g. a spliced `N` in the CIGAR) |
| `seq` | the read's full sequence |
| `qual` | the read's full quality string (Phred+33 ASCII) |
| `ncig` | number of CIGAR operations |

All columns are queryable in `where`/`and`/`or`/`order` commands — it's a real SQL table. The first twelve (`qname` through `tags`) can also be displayed in the info pane with `show`/`hide` commands; the rest (`indel` through `ncig`) exist for querying but are not directly displayed in tabular format.

For advanced users: most of these map directly onto fields in htslib's `bam_pileup1_t` and `bam1_t` structs, if you want to cross-reference against htslib's own documentation.

#### Query Walkthrough

Say you're looking at a locus and want to check for mismapping artifacts. Start broad — every non-reference base at this position, some of which might just be sequencing noise:
```
where base != 'G'
```
Narrow to reads that are more likely trustworthy — FLAG bits 9/10/11: not supplementary, not a duplicate, not failing vendor QC:
```
and (flag & 3584) = 0
```
Then sort to see the weakest evidence first:
```
order basequal
```
Three REPL commands, each a plain SQL fragment — an inequality, a bitwise flag check, a column to sort by. The parser wraps these commands into a complete SQL statement. Note that you could also write the full command as a single statment:
```
where base != 'G' AND (flag & 3584) = 0 ORDER BY basequal ASC
```
The two styles are equally supported; you can continue to add on further clauses with `and` and `or` as you like.

If you want a count rather than a filtered view, `count [clause]` answers without disturbing the active query. For example, `count mapq < 20` tells you how many low-mapping-quality reads are in the current view without adding a WHERE clause you'd have to undo afterwards with `back`.

#### Further Examples

Beyond plain comparisons, SQLite's full function library is available in these fragments too. A few examples:

**Motifs at the query position.** `qpos` is the 0-based offset into `seq` for the base at the pileup position, and SQLite's `substr()` is 1-based, so the query base itself is `substr(seq, qpos + 1, 1)`, and a motif starting there is:
```
where substr(seq, qpos + 1, 4) = 'GATC'
```
`LIKE` (`_`/`%`) and `GLOB` (`?`/`*`/`[ACG]`) both work as wildcards on top of that — `GLOB`'s character classes are handy for IUPAC-style ambiguity:
```
where substr(seq, qpos + 1, 3) glob 'A[CG]T'
```
To search a window around `qpos` rather than anchored exactly on it, pull the window first and search inside it:
```
where instr(substr(seq, qpos - 2, 10), 'GATC') > 0
```

**Aux tags.** `tags` is a JSON blob of the read's aux tags — pull a specific one out with `json_extract` or the `->>` shorthand:
```
where tags ->> '$.NM' > 2
```
or checking a read group:
```
where tags ->> '$.RG' = 'sample1'
```
A read with no aux tags, or missing that specific tag, comes back as SQL `NULL` rather than an error, so `where tags ->> '$.RG' is null` finds reads missing that tag — no extra guard needed.

**Soft-clipping.** `cigar` is a plain string, so text matching works directly on it — reads with any soft-clipping at all:
```
where cigar like '%S%'
```

More is possible — `cigar`/`tags`/`seq` are just text and JSON in SQLite, so anything its function library can do to a string or JSON, it can do here. See [SQLite's expression/function reference](https://sqlite.org/lang_expr.html).

### A Word on `dump` Functionality

A dump is a small, self-contained sqlite3 file with just the reads at this one locus — not the whole BAM. Picking a session back up later with `apb db` is one reason to use it; a few others:

- **Full, unrestricted SQL** — `sqlite3 my.db` gets you everything the in-TUI REPL deliberately doesn't: joins, `GROUP BY`, aggregates, whatever else. The REPL is for quick filter/sort while browsing; the dumped file allows for more complex analysis if needed.
- **Downstream use** — it's a normal sqlite3 file, so anything with a sqlite driver can read it.
- **Sharing** — send a colleague exactly the reads you're looking at, at a fraction of the size, without them needing the original BAM/CRAM or a genome browser of their own.
- **Debugging** (for developers) — a stable snapshot of exactly what got loaded, inspectable without the original alignment file or the TUI. Mostly relevant if you're developing `apb` itself, rather than just using it.

### A Word on Indexing Systems

`htslib`/`samtools`/`bcftools`, and by extension all alignment and VCF data, work across 3 (3!!) coordinate systems. This can be tricky to navigate.

`apb` uses 0-based half-open coordinates throughout, **except for the locus argument when starting `apb` from the command line, which is 1-based**. A 1-based locus argument has the advantage of being identical to the VCF `POS` field per the VCF specification. However, `htslib`'s internal alignment representation format is 0-based, so it is more natural (and less bug-prone) to display that information as 0-based. This is an inevitable UX compromise - feedback is appreciated.

## Future Roadmap

### Planned
- Indication of insertion sites (easily implemented, but the best way to display them is unclear).
- Pannable alignment view (currently the view is only scrollable up/down - side to side is planned).
- Fold-out display of quality string below each aligned read.
- Minor UX/UI improvments

### Speculative
These are items that I think might be useful,
but are more work so I will only add them if users
find them desirable.

- Multiple alignment pileups.
- Locus-jumping from within TUI when reading an alignment file - e.g. `goto chr1:2500`.
  - Currently the view is fixed to a single locus specified at startup.
  - This may also lead to multi-locus dbs, multi-sample browsing/dbs, etc.
- VCF-driven locus browsing - input a VCF along with alignment/s and navigate between variant loci.
  - unlikely to implement any filtering of the vcf as that can be done at or before startup with `bcftools` and shell piping/substitution.

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
Coverage is concentrated on the backend: schema/pragma setup, pileup-to-row ingestion (CIGAR stringification, aux-tag-to-JSON conversion, NULL handling), and the dynamic WHERE/ORDER BY query builder behind the REPL. TUI rendering and event handling aren't unit tested — there's no headless termbox harness — so changes there are checked by running the app directly (`demo` mode is the quickest way) rather than via `ctest`.

### AI Usage Policy

This repo has been developed by hand, with use of Claude Code as a second line — for design discussion, bug hunting, and basic stub implementation. Architecture, the design of all core primitives and functions, and other impactful decisions are made by the maintainer. Small, mechanical, additive changes (a keybinding, a warning fix, a rename) might be handed over. A new logical unit — a new subsystem, a new abstraction — is not; those are designed and implemented manually. The benefit is a codebase that is (hopefully) well-designed, effective, and concise - and therefore easy to maintain and easy to contribute to. Contributions would ideally follow this standard.

