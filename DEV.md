# Development

This document...

## Open Feature Work

See the roadmap in [README.md](README.md).

## Open Non-Feature Work

- Consistently assert invariants and preconditions in all functions
- Currently the demo mode database is fixed at compile time, meaning different builds will have different demo data.
  The demo data is generated in a parameterised manner so this isn't a big problem but it would be nice if a single
  demo database could be distributed within the src.

## Development Adages

Things to keep in mind when working on `apb`:

- Good error handling is paramount.
  - Exceptions are not a good way to handle errors. Err/Status types and expected<T, E> are.
  - Prefer tightly-scoped error types over shared error types - the shared type
    just leads to pushing the error back up the chain without ever properly handling it.
    Scoped types can feel a bit clunky but I think they're worthwhile.
  - Where a function returns no result but could error, return a Status type of appropriate richness.
  - where a function returns a result but may error, use expected<T, E> with an E type of appropriate richness.
  - When wrapping calls to a c library where the error space is scoped to the error space of that c library,
    return the c library error type (usually raw ints) directly for caller to handle.
  - It is fine that the error handling is quite varied within the codebase. Different situations require
    different approaches.

- Things will go wrong when released to the public, make them go wrong well.
  - `APB_ASSERT()` and `APB_UNREACHABLE()` exist for this purpose, and prevent
    crashes from breaking the user terminal.

- Add assertions and precondition checks to all functions.
  - This is a very revealing as a practice!
  - It's a lot easier to keep track of preconditions if you don't modularise/split large
    functions unneccessarily.
  - Asserting what you believe to be true makes it much easier to implement your intention.
  - Regular use of assertions make debugging much easier.

- Braces avoid issues with variable initialisation when jumping in goto or switch case (I forget this sometimes).
  - default to using braces with case statments!

I am almost certainly not using these reliably, but I do find them helpful.

## Use of Hungarian Notation

**o_** - owned pointer, this scope must handle lifetime.  
**br_** - borrowed pointer, this scope must not affect lifetime.  
**k** - file-level constant used across multiple scopes. I don't use an underscore with this one.

The pointer prefixes are mostly for use with C libraries.
As above, I am almost certainly not using these reliably, but again I find them helpful.

## Dependencies

| Dependency | Version | Found via |
|---|---|---|
| sqlite3 | ≥3.38 | system, `pkg-config` |
| htslib | ≥1.17 | system, `pkg-config` (or `-DHTSLIB_INCLUDE_DIR`/`-DHTSLIB_LIBRARY`) |
| termbox2 | 605398fa | Vendored |
| plog | v1.1.10 | Vendored |
| fmt | v12.2.0 | Vendored |
| Catch2 [optional] | v3.8.1 | CMake FetchContent |

## AI Usage

This repo has been developed by hand, with some use of AI tools for extraneous work like implementing githooks etc.
The design and implementation of all core types and logic are made by the maintainer.
Contributions are more than welcome, but would ideally follow this standard.
