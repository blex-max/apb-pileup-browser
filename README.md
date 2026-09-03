# `apb` - A Pileup Browser

`apb` is a terminal genome browser tailored to exploratory viewing and querying of pileups of mapped reads at genomic loci.
It features an SQL-based command line for querying reads at the selected locus,
coupled with an alignment display showing reads aligned to their genomic positions,
and their divergence from a reference genome. It is chiefly designed for verification and investigation of variant calls, but can be used to
inspect the reads at any loci.
The built-in command line is capable of highly complex queries, but is tuned to make exploratory pattern hunting quick and seamless.

```
          READS ALIGNED TO REFERENCE ↓                                PER READ DATA ↓
╭──────────────────────────────────────────────────────────────┬──────────────────────────────────────────────────────────────╮
│GTACGTACGTACGTACGTACGTACGTACGTAAGTACGTACGTACGTACGTACGTACGTACGT│  basequal  │ flag │   cigar   │        qname        │        │
├───────────────────────────────|──────────────────────────────│──────────────────────────────────────────────────────────────┤
│===========================G======                            │37          │99    │149M       │read41               │        │
│=====================C=====A=======                           │37          │99    │149M       │read2                │        │
│=================================̊==                           │37          │99    │147M3I2M   │read80               │        │
│                                ^CCG                          │            │      │           │                     │        │
│===============================T̊===                           │37          │99    │146M2I3M   │read78               │        │
│                               ^CC                            │            │      │           │                     │        │
│====================================                          │37          │99    │149M       │read81               │        │
│===============================T========                      │37          │99    │149M       │read64               │        │
│========================================̊=                     │37          │99    │148M4I1M   │read10               │        │
│                                       ^CTGA                  │            │      │           │                     │        │
│=================================̊=========                    │37          │99    │140M1I9M   │read20               │        │
│                                ^G                            │            │      │           │                     │        │
│===============================T================              │37          │99    │149M       │read7                │        │
│=================================================             │37          │99    │149M       │read71               │        │
│===============================T====================          │37          │99    │149M       │read67               │        │
│=================================================̊===          │37          │99    │146M2I3M   │read66               │        │
│                                                ^CA           │            │      │           │                     │        │
│=======================G=======T=============s(8)             │37          │99    │141M8S     │read44               │        │
│===============================T==========s(12)               │37          │99    │137M12S    │read8                │        │
│======================================================        │37          │99    │149M       │read21               │        │
│=================================================A=====       │37          │99    │149M       │read53               │        │
│========================================================T=    │37          │99    │149M       │read63               │        │
│=======A====================================̊==============    │37          │99    │135M2I14M  │read27               │        │
│                                           ^GC                │            │      │           │                     │        │
│===============================T=========C================    │37          │99    │149M       │read32               │        │
├──────────────────────────────────────────────────────────────┴──────────────────────────────────────────────────────────────┤
│ LOCUS: demo:149 │ SPAN: 3-294 │   ← LOCUS INFO                                                                              │
╭━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╮
│WHERE basequal >= 37 and mapq != 0   ← ACTIVE QUERY                                                                          │
│─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────│
│:where base != 'A'                   ← COMMAND LINE                                                                          │
│─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────│
│OK!                                  ← RETURN MESSAGES                                                                       │
╰                                                                                                                             ╯
```

A basic text screencap of the TUI - explanatory notes are in CAPITALS. The real TUI has richer, more readable styling.
`=` matches the reference, `-` is a deletion, and a ring over a base marks an insertion, displayed beneath at `^`.

Advantages:
- Immediately available in the terminal; no spinning up a genome browser instance or navigating a web UI.
- Easily installed, including on compute cluster nodes.
- Fast; no network IO, responsive UI.
- UI optimised for one job - inspecting pileup loci - rather than general-purpose genome browsing.
- Powerful SQL-backed query syntax for fast exploration.

This software is in a demo state and feedback is very much appreciated as I work towards a 1.0 release!

For CLI/TUI usage, command syntax, and query examples, see [MANUAL.md](MANUAL.md), or generate it yourself with `apb --dump-manual <path>`.

## Install

You will need a terminal emulator with basic unicode support. I expect the TUI should render successfully on almost any modern-ish emulator.
If you have issues with rendering please report them. `apb` has been confirmed to work in iTerm2, ghostty, vscode, and tmux.

Docker images are provided via the repo GitHub; check the `packages` tab to pull the latest release with docker, singularity, etc. This is
the easiest way to get `apb`.

### Building from source

Requires:
- a C++23 compiler (gcc ≥ 12 and clang ≥ 21 are both known to work)
- CMake ≥ 3.22
- `pkg-config`
- `sqlite3` ≥ 3.38
- `htslib` ≥ 1.17

`sqlite3` and `htslib` need to be discoverable via `pkg-config`. If htslib isn't packaged that way on your system, you can point the build
process at it directly with `-DHTSLIB_INCLUDE_DIR` and `-DHTSLIB_LIBRARY`. All other dependencies for the main binary are vendored with the
source. The test binary uses Catch2, which is pulled in via FetchContent if `-DMAKE_TEST=0` is passed to the cmake configure step.

```sh
cmake -S . -B build
cmake --build build
```

The compiled binary can be found at `build/apb`.

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
- More stats in the status bar; allele counts, VAF (when in variant driven mode), reference span complexity assessment (useful when
assessing artefactual variants).

### Speculative
These are items that I think might be useful,
but are more work so I will only add them if users
find them desirable.

- Multiple alignment pileups.
- Locus-jumping from within TUI when reading an alignment file - e.g. `goto chr1:2500`.
  - Currently the view is fixed to a single locus specified at startup.
  - This may also lead to multi-locus dbs, multi-sample browsing/dbs, etc.


## Development

### Use of Hungarian Prefixing

**o_** - owned pointer, this scope must handle lifetime.
**br_** - borrowed pointer, this scope must not affect lifetime.
**sh_** - shared statically-allocated (probably) object, not defined in this scope.
**ru_** - buffer reused across loop iterations.

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

I think it's important to be up front about AI usage. This repo has been developed by hand, with use of AI as a second line — for bouncing
ideas off of, bug hunting, and basic stub implementation. Architecture, the design of all core primitives and functions, and other impactful
decisions are made by the maintainer. Small, mechanical, additive changes (a keybinding, a warning fix, a rename) might be handed over. A
new feature or refactor is not; those are designed and implemented manually. The benefit is a codebase that is (hopefully) well-designed,
effective, and concise - and therefore easy to maintain and easy to contribute to. Contributions are more than welcome, but would ideally
follow this standard.

