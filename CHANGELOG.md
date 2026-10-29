# LOG

User-facing changes will be listed here for each release

## v0.4.0

- Command aliases added for those used to capitalised SQL (`WHERE`, `AND`, `OR`, `ORDER BY`).
- Parsing added to allow direct SQL to be provided at the command line, e.g.: WHERE ... ORDER BY ...
- `Ctrl-r` keybind added to retrieve current query from display status bar for editing and resubmission at the command line.
- `Ctrl-u` and `Ctrl-d` keybinds added for stepping command history, for those on terminals which swallow `shift-Up`/`shift-Down`.
- Improved queryin examples in manual.

## v0.3.0

- Performance enhancements for high depth loci.
- `order-by` is now `sort` (with alias `order`).
- Made memory usage limiting opt-out at the CLI.
- Squashed some edge case rendering artifacts.

## v0.2.0

- Added multi-alignment pileups
- Log began
