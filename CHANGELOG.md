# CHANGELOG

## v0.4.0 (release pending)

### User-facing
- Command aliases added for those used to capitalised SQL (`WHERE`, `AND`, `OR`, `ORDER BY`).
- Parsing improved to allow direct SQL to be provided at the command line, e.g.: WHERE ... ORDER BY ...
- `Ctrl-r` keybind added to pull current query from display status bar into the command line, for editing and resubmission.
- `Ctrl-u` and `Ctrl-d` keybinds added for stepping command history, for those on terminals which swallow `shift-Up`/`shift-Down`.
- schema update; database dumps from previous versions will no longer load.
- Improved querying examples in manual.
- Improved demo data.

### Internal
- code restructing to be more flexible and frontend agnostic.

## v0.3.0

- Performance enhancements for high depth loci.
- `order-by` is now `sort` (with alias `order`).
- Made memory usage limiting opt-out at the CLI.
- Squashed some edge case rendering artifacts.

## v0.2.0

- Added multi-alignment pileups
- Log began
