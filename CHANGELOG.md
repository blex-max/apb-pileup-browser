# CHANGELOG

## v0.5.0

A fairly small release focused on compatibility issues.

### Highlights:

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
