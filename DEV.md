# Development

This document forms the todo list and development guide for `apb`.

Before merging to main/making a release, please run through [PRERELEASE.md](PRERELEASE.md).

## Open Feature Work

See the roadmap in [README.md](README.md).

## Open Non-Feature Work

<not fully up to date - more to be added>

- Currently the demo mode database is fixed at compile time, meaning different builds will have different demo data.
  The demo data is generated in a parameterised manner so this isn't a big problem but it would be nice if a single
  demo database could be distributed within the src.
- Not clear exactly what values of the TERM env variable apb supports - need to investigate and document.
- introduce internal benchmarking via creation of hefty demo dbs and timing apb operations on them.
- very long commands/statuses/messages will be clipped - prompt input should auto scroll with long typing, and messages should be truncated with an ellipis.
- logs contains literal control characters, which should be escaped before entering into log.
- Read a config toml/yaml at the CLI, indicating colour scheme, startup table columns and startup toggle/setting states.
- tab-autocomplete for column names at command prompt

## Development Adages

Things to keep in mind (for myself as much as anyone) when working on `apb`:

- Good error handling is valuable.
  - Prefer Err/Status types and/or expected<T, E> over exceptions.
    I find exceptions harder to reason about.
  - Where a function returns no result but could error, return a Status type of appropriate richness.
  - where a function returns a result but may error, use expected<T, E> with an E type of appropriate richness.
  - When wrapping calls to a c library where the error space is scoped to the error space of that c library,
    return the c library error type (usually raw ints) directly for caller to handle.
  - If a function requires a clunky or complicated type to represent it's possible return status/errors,
    then it's probably doing too much and should be split up and composed.

- Things will go wrong when released to the public, make them go wrong well.
  - There are various apb macros for this purpose, which prevent
    crashes from breaking the user terminal.
  - It's preferable to crash than to potentially mislead the user by presenting
    wrong or corrupted data.

- Add assertions and precondition checks to all functions.
  - This is a very revealing as a practice!
  - Asserting what you believe to be true makes it much easier to implement your intention.
  - Regular use of assertions make debugging much easier.
  - Assertions are for checking that the code itself is well-formed, not verifying the data integrity

- It's a lot easier to keep track of preconditions if you don't modularise/split
  functions unnecessarily.

- Braces avoid issues with variable initialisation when jumping via goto or switch case (I forget this sometimes).
  - default to using braces with case statements!

I am almost certainly not using these reliably, but I do find them helpful.

## Use of Hungarian Notation

**o_** - owned pointer, this scope must handle lifetime.  
**br_** - borrowed pointer, this scope must not affect lifetime.  
**g_** - global state.  
**_0ho** - 0-based half-open coordinates  
**_1c** - 1-based closed coordinates

The pointer prefixes are mostly for use with C libraries.
As above, I am almost certainly not using these reliably, but again I find them helpful.

## Layout terminology

The top widget is the "read viewer", which has an "alignment pane" on the left and a "table pane" on the right.
The lower widget is the "command prompt".

## Dependencies

Dependencies will be properly credited in the README in future.

| Dependency | Version | Found via |
|---|---|---|
| sqlite3 | ≥3.38 | system, `pkg-config` |
| htslib | ≥1.17 | system, `pkg-config` (or `-DHTSLIB_INCLUDE_DIR`/`-DHTSLIB_LIBRARY`) |
| termbox2 | 605398fa | Vendored |
| fmt | v12.2.0 | Vendored |
| doctest | v2.5.3 | Vendored |

