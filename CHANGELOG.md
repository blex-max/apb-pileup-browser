# CHANGELOG

## v0.4.0 (release pending)

### Highlights:

- New sparkline view of base qualities (`▁▂▃▄▅▆▇█`), via `toggle sparkline`.
- Recall and edit current query. `Ctrl-r` now retrieves the current query and places it
  in full at the command line, for editing and resubmission.
- Layout changes:
  - Command view is now more compact to give more room for visual pattern finding and a cleaner interface.
  - The total read count and count for the current query are always displayed.

### User-facing
- Command aliases added for those used to capitalised SQL (`WHERE`, `AND`, `OR`, `ORDER BY`).
- Parsing improved to allow direct SQL to be provided at the command line, e.g.: WHERE ... ORDER BY ...
- `back` command has become `undo`, and behaviour has been made more intuitive.
- `track` and `show-table` replaced by `toggle` (`tg`), which toggles display properties, including `quality`, `insertion`, `table` and `sparkline`.
- `count` now requires a condition, as counts are always displayed for the current query.
- `sort` accepts `order by` / `ORDER BY` as aliases.
- `and` now adds parentheses only where operator precedence requires:
  - `and` wraps the existing query, and the new condition, if either contains a top-level `OR`.
  - Previously the existing query was always left-wrapped and a new `and`/`or` condition could corrupt meaning.
- Improved demo data.
- Improved manual.

### Keybinds
Keybinds updated to fix issues with default bindings on macOS terminal and align better with GNU readline.
- Added: `Ctrl-p`/`Ctrl-n` (command history), `Home`/`End`, `Del`, `Ctrl-Left`/`Ctrl-Right` and `Alt-b`/`Alt-f`, `Ctrl-w`, `Alt-d`, `Ctrl-k`, `Ctrl-u`.
- Changed: `Alt-Bksp` deletes a word, not the whole line.
- Removed: `Shift-Up`/`Shift-Down` command history stepping.

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
