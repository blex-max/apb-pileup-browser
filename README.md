# apb - A Pileup Browser

`apb` is a terminal genome browser tailored to exploratory viewing and querying of variant loci.
It features an SQL-based command line for querying reads at the selected locus,
coupled with an alignment display showing reads aligned to their genomic positions,
and their divergence from a reference genome.
The command line is capable of highly complex queries, but tuned to make exploratory pattern hunting quick and seamless.

<!-- TODO: use case examples - basically, variant assessment and tool assessment -->

Advantages:
- Right there in the terminal, no spinning up genome browser instances or navigating via web browser. Can be installed on clusters and used right wehre the data is.
- Fast
- Since the aim is to serve a specific niche (looking at pileup loci), UI is optimised to that end
- Powerful query language (thanks to sqlite)

This software is in a demo state and feedback is very much appreciated as I work towards a 1.0 release!

<!-- TODO must include an image/s -->

## Install

Requires a C++23 compiler, CMake ≥3.22, and `pkg-config`. `sqlite3` (≥3.38) and `htslib` (≥1.14) need to be discoverable via `pkg-config`; if htslib isn't packaged that way on your system, point the build process at it directly with `-DHTSLIB_INCLUDE_DIR` and `-DHTSLIB_LIBRARY`. Everything else (termbox2, plog, argparse) is pulled automatically via CMake FetchContent, so the first configure needs network access.

```sh
cmake -S . -B build
cmake --build build -j
```

The compiled binary can be found at `build/apb`.

## CLI Usage

The CLI is in a demo state - the subcommand approach might not be long term.

```sh
apb sam <alignment-file> <locus> [--ref reference.fasta] [--dump out.db]
apb db <dumped.db>
apb demo [--dump out.db]
```

`sam` opens a live alignment file (SAM/BAM/CRAM) at a locus (`chr1:12345`) and launches the TUI. `--dump` skips the TUI entirely: it builds the pileup, writes it straight to a sqlite3 database file, and exits — useful for headless/batch use.

`db` reopens a database previously produced by `--dump` (or the in-TUI `dump` command), so you can come back to a pileup without re-reading the original alignment file.

`demo` runs against synthetic data, no alignment file required — good for a first look at the tool.

All subcommands accept `--log PATH` to write debug output to a file.

## TUI Usage

Note that this is all subject to change pending user feedback.

<!-- Not well structured. The ability for a column to be shown in the data view should be separated from field meaning into a separate table for one, and more generally each section should be checked such that they naturally build on each other and everythign is introduced in the right order -->

### How It Works

Point `apb` at an alignment file and a locus (`apb sam <file> <locus>`), and it builds the pileup at that single position with htslib, then loads every overlapping read into a `reads` table in an in-memory sqlite database — one row per read. That table is the single source of truth for everything else: the alignment pane on the right renders it, and the command line queries it directly with plain SQL `WHERE`/`ORDER BY`. `--dump` (or the in-TUI `dump` command) writes that database out to a file, and `apb db <path>` reopens one later without needing the original alignment file again.

Here's every column in the `reads` table — what it means, and whether it can also be shown in the alignment pane with `show`/`hide`:

<!-- NOTE: There are other fields, but those are for backend use only -->
| Column | Meaning | show/hide? |
|---|---|---|
| `qname` | read/template name (QNAME) | Yes |
| `flag` | SAM bitwise FLAG | Yes |
| `rstart` | 0-based leftmost mapping position | Yes |
| `rend` | 0-based rightmost mapping position | Yes |
| `mapq` | mapping quality | Yes |
| `base` | the read's base at the pileup position | Yes |
| `basequal` | base quality (Phred) at the pileup position | Yes |
| `qpos` | 0-based offset into `seq`/`qual` for this position | Yes |
| `cigar` | full CIGAR string | Yes |
| `mtid` | reference name of the mate/next read | Yes |
| `mstart` | mate/next read's leftmost mapping position | Yes |
| `tags` | aux tags as JSON — able to be individually queried | Yes |
| `indel` | indel length to the next position (0 none, >0 insertion, <0 deletion) | No |
| `is_del` | 1 if this position is a deletion | No |
| `is_head` | 1 if this is the read's first aligned base | No |
| `is_tail` | 1 if this is the read's last aligned base | No |
| `is_refskip` | 1 if this position is a reference skip (e.g. a spliced `N` in the CIGAR) | No |
| `seq` | the read's full sequence | No |
| `qual` | the read's full quality string (Phred+33 ASCII) | No |
| `ncig` | number of CIGAR operations | No |

All of these are queryable in `where`/`and`/`or`/`order` clauses — it's a real SQL table. Only the first twelve are also toggleable in the display.

### Basic Navigation

Normal typing goes to the command line; `Enter` dispatches it as a command (see [The Command Line](#the-command-line) below). Navigation keys navigate the alignment view, and the command line.

**Browser pane**
- `Shift+↑` / `Shift+↓` scroll the alignment view by one row.
- `PgUp` / `PgDn` scroll by a full page.

**Command line**
- `↑` / `↓` step through command history.
- `←` / `→` move the cursor; `Ctrl-A` / `Ctrl-E` jump to start/end; `Alt+←` / `Alt+→` (or `Alt+b` / `Alt+f`) jump by word.
- `Backspace` deletes a character; `Alt+Backspace` clears the whole line.

### The Command Line

<!-- Not a great intro, just dives in too headfirst -->
Filtering and sorting uses simple boolean conditions on those columns: `where mapq >= 30` keeps only confidently-mapped reads; `where base = 'A' and flag & 16 = 0` narrows to forward-strand reads with an A at the query position. No deep knowledge of SQL is needed for any of this — it's just comparisons (`=`, `!=`, `<`, `>`) joined with `and`/`or`. It happens to compile down to a real SQL `WHERE` clause, which lets things scale up to genuinely complex queries later if you want them (more on that in Query Syntax in Depth, below).

Every line typed at the command line is dispatched as `<command> [args]`. The active query — that WHERE clause plus an ORDER BY — persists across commands and is built up a piece at a time rather than retyped from scratch each time.
<!-- Doesn't really get across the why of the piecewise build up. It's to support exploration, which raw sql doesn't really do. Normally, sql is structured for firing off a known query and getting back a result. Here, the expected use case is more like, lets see which reads have a different base to the reference. Oh, that's interesting, lets see which of those are low quality. oh, now lets filter by flag. Oh, that's not informative, lets go back to the previous view, and so on -->

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

### Query Syntax in Depth


<!-- these two intro paragraphs are a great opportunity to give an example of the exploratory mode! trying one thing, then having a new idea and trying the next. Not least because the example can actually be typed in a single where - and that's ok too, and intentionally supported! -->
Start simple. Say you want to look at non-reference bases from clean reads (no secondary/duplicate/QC-fail), sorted by base quality — three REPL commands, typed one at a time:
```
where base != 'G'
and (flag & 3584) = 0
order basequal
```
Each line is just a command (`where`/`and`/`order`) followed by a plain SQL fragment: an inequality, a bitwise flag check, then a column to sort by. No `SELECT`, no `FROM reads`, no spelling out `ORDER BY` — the REPL supplies all of that; you only ever type the fragment. That covers most day-to-day use.

<!-- Should include a reminder about the count functionality -->

<!-- This is important to detail, but is quite randomly placed -->
FLAG is a bitmask, so `&` is how you pick out specific bits — `3584` above is bits 9/10/11 (not supplementary, not a duplicate, not failing vendor QC); `flag & 4 = 0` (bit 2) keeps only mapped reads. Combine as many bits as you need in one expression.

Beyond plain comparisons, SQLite's full function library is available in these fragments too. A couple of the more useful ones:

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

<!-- Another example case worth having is querying cigar strings for e.g. reads with soft clipping -->

<!-- Must add that much more is possible, and link to sqlite tutorial webiste -->

### A Word on dump Functionality

A dump is a small, self-contained sqlite3 file with just the reads at this one locus — not the whole BAM. Picking a session back up later with `apb db` is one reason to use it; a few others:

- **Full, unrestricted SQL** — `sqlite3 my.db` gets you everything the in-TUI REPL deliberately doesn't: joins, `GROUP BY`, aggregates, whatever else. The REPL is for quick filter/sort while browsing; the dumped file allows for more complex analysis if needed.
- **Downstream use** — it's a normal sqlite3 file, so anything with a sqlite driver can read it.
- **Sharing** — send a colleague exactly the reads you're looking at, at a fraction of the size, without them needing the original BAM/CRAM or a genome browser of their own.
- **Debugging** (for developers) — a stable snapshot of exactly what got loaded, inspectable without the original alignment file or the TUI. Mostly relevant if you're developing `apb` itself, rather than just using it.

## Future Roadmap

### Planned
- Indication of insertion sites (easily implemented, but the best way to display them is unclear).
- Pannable alignment view (currently the view is only scrollable up/down - side to side is planned).
- Fold-out display of quality string below each aligned read.

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

| Dependency | Version | Brought in via | Used for |
|---|---|---|---|
| sqlite3 | ≥3.38 | system, `pkg-config` | the whole query/storage layer — `reads`/`loci`/`metadata` tables, the WHERE/ORDER BY REPL, `--dump`/`db` |
| htslib | ≥1.14 | system, `pkg-config` (or `-DHTSLIB_INCLUDE_DIR`/`-DHTSLIB_LIBRARY`) | reading SAM/BAM/CRAM and reference FASTAs, building the pileup |
| termbox2 | pinned commit | vendored, CMake FetchContent | terminal rendering and raw input events |
| plog | v1.1.10 | vendored, CMake FetchContent | debug logging to `--log` |
| argparse | v3.2 | vendored, CMake FetchContent | CLI subcommands/flags |
| Catch2 | v3.8.1 | vendored, CMake FetchContent, only with `-DMAKE_TEST=ON` | test framework |

The vendored ones are pinned to an exact tag/commit in `CMakeLists.txt` and fetched fresh on first configure — nothing's vendored into the repo itself.

### Tests

Test files live in `tests/`. Build and run them with:
```sh
cmake -S . -B build -DMAKE_TEST=ON
cmake --build build -j
ctest --test-dir build
```
Coverage is concentrated on the backend: schema/pragma setup, pileup-to-row ingestion (CIGAR stringification, aux-tag-to-JSON conversion, NULL handling), and the dynamic WHERE/ORDER BY query builder behind the REPL. TUI rendering and event handling aren't unit tested — there's no headless termbox harness — so changes there are checked by running the app directly (`demo` mode is the quickest way) rather than via `ctest`.

### AI Usage Policy

This repo has been developed by hand, with use of Claude Code as a second line — for design discussion, bug hunting, and basic stub implementation. Architecture, the design of all primitives, and other impactful decisions are made by the maintainer. Small, mechanical, additive changes (a keybinding, a warning fix, a rename) might be handed over. A new logical unit — a new subsystem, a new abstraction — is not; those are designed and implemented manually. The benefit is a codebase that is (hopefully) well-designed, effective, and concise - and therefore easy to maintain and easy to contribute to. Contributions would ideally follow this standard.

