# CHANGELOG

## 0.6.0

- DB is now positional: `apb my.db` replaces `apb --db my.db`.
- Soft-clipped bases may now be shown in brackets beside the alignment, e.g. `(ACGT)====`, in place of the `s(N)` label. `toggle clips` restores the label.
- New `toggle mask`: with mask off, bases matching the reference are shown in full (dimmed) rather than as `=`, with mismatches in bold underline.
- New `toggle anchor` shows or hides the ring over the base before an insertion.
- New `toggle colour` (or `color`) colours the reference and read bases not matching it: A green, C blue, G yellow, T red.
- New `-s`, `--settings` sets display options on startup, e.g. `--settings mask=off,colour=on`, using the option names of `toggle`.
- In multi-file view, each file heading shows how many of its reads match the query, e.g. `a.bam: 12/300 reads`.
- Runtime memory usage is now minimal for most use cases and bounded to approximately the usage of the htslib pileup engine for large pileups
  - apb streams transformed records to the database without holding them in memory.
  - apb now loads pileups into a temporary database file (under SQLITE_TMPDIR or TMPDIR, deleted on exit) rather than holding them in memory, and opens DB directly (read-only) rather than copying it into memory.
- Reads that fail apb's data checks while loading (e.g. a non-standard FLAG bit) now report which check failed.

- Fixed a bug where reads terminated with both a hard and soft clip operation would show only the outer hard clip label.
- Fixed a bug where overlapping insertions would overwrite eachother in the TUI. Overlapping insertions are now shown in separate rows. 
- Fixed a crash when opening a DB whose reads' sequence or qualities don't match their CIGAR, or which extend beyond the locus span; such DBs are now rejected with an error naming the read.
- Fixed `mtid` showing the contig name instead of `=` for mates on the locus contig when the locus is given by a header alias (`@SQ ... AN:`).

- Changes to underlying schema; schema version now v3.

## v0.5.1

Patch to make modal interface clearer in helptext

## v0.5.0

apb now has a modal interface. `Esc` switches focus between the read viewer and the command prompt. This removes the need for so many unique keybindings to cover both the viewer and the prompt at once. Some of these bindings have been found to be unreliable across different setups and terminal emulators, so this change should hopefully address that problem.

Both the read viewer and the command prompt are now navigated by unmodifed arrow keys, which feels more intuitive. See `help nav` for the keybindings for each mode, or consult the manual.

- Esc now switches between modes.
- Ctrl-C always quits.
- Up/Down replace C-p/C-n for command history when the prompt is focused.
- Left/Right replace Shift-Left/Right for panning when the read viewer is focused.
- The help overlay closes with Esc, not `q`.
- The `quit` command is now available again at the prompt.
- `--log` now takes a PATH.

## v0.4.0

### Highlights:

- Query recall/edit replaces `and`/`or`
  `Ctrl-q`, `Ctrl-w` and `Ctrl-o` now grab the current query and place it
  at the command prompt, for editing and resubmission.
    - `Ctrl-q`: grab entire query.
    - `Ctrl-w`: grab WHERE clause.
    - `Ctrl-o`: grab ORDER BY clause.
  Improved keybinds for moving/editing text make this approach effective.
- Layout changes:
  - Command view is now more compact to give more room for visual pattern finding and a cleaner interface.
  - The total read count and count for the current query are always displayed.
- New sparkline view of base qualities (`▁▂▃▄▅▆▇█`) when displaying sequence quality track, enabled via `toggle sparkline`.

### User-facing
- Command aliases added for those used to capitalised SQL (`WHERE`, `ORDER BY`).
- Parsing improved to allow direct SQL to be provided at the command prompt, e.g.: WHERE ... ORDER BY ...
- `track` and `show-table` replaced by `toggle` (`tg`), which toggles display properties, including `quality`, `insertion`, `table` and `sparkline`.
- `count` now requires a condition, as counts are always displayed for the current query.
- `count all` command added to count reads matching <condition> ignoring current WHERE filter.
- `sort` is now `order by` (aliases `order`, `o`, `ORDER BY`, `sort`).
- `Ctrl-/` undoes the last modification made to the query.
- `back` command has been removed in favour of `Ctrl-/` undo.
- `quit` command has been removed in favour of `Ctrl-c`
- Improved demo data.
- Improved manual.

### Keybinds
Keybinds updated to fix issues with default bindings on macOS terminal and align better with GNU readline.
Keybinds added for query recall, editing and undo.
See Manual for details

### Database
- Schema v2. Database dumps from previous versions will no longer load.

## v0.3.0

- Performance enhancements for high depth loci.
- `order-by` is now `sort` (with alias `order`).
- Made memory usage limiting opt-out at the CLI.
- Squashed some edge case rendering artifacts.

## v0.2.0

- Added multi-alignment pileups
- Log began
