# `apb` - A Pileup Browser

`apb` is a terminal genome browser tailored to exploratory viewing and querying of pileups of mapped reads at genomic loci.
It features an SQL-based command line for querying reads at the selected locus,
coupled with an alignment display showing reads aligned to their genomic positions,
and their divergence from a reference genome. It is chiefly designed for verification and investigation of variant calls, but can be used to
inspect the reads at any loci.
The built-in command line is capable of highly complex queries, but is tuned to make exploratory pattern hunting quick and seamless.


A text screencap of the TUI. Line wrapping may break this screencap!
```
          ALIGNMENT DISPLAY PANE ↓                                                      PER READ DATA TABLE ↓
╭─────────────────────────────────────────────────────────────────────────────────────┬─────────────────────────────────────────╮
│TACGTACGTACGTACGTACGTACGTACGTACGTACGTACGTAAGTACGTACGTACGTACGTACGTACGTACGTACGTACGTACGT│  basequal  │ flag │ mapq │   cigar   │  │
├──────────────────────────────────────────|──────────────────────────────────────────│─────────────────────────────────────────┤
│===============A==========================T===                                       │37          │0     │17    │149M       │  │
│==========================================T=======                                   │37          │0     │15    │149M       │  │
│============================A=============T========                                  │37          │0     │27    │149M       │  │
│==================================================̊====                               │37          │0     │11    │145M2I4M   │  │
│                                                 ^CG                                 │            │      │      │           │  │
│==========================================T===========                               │37          │0     │56    │149M       │  │
│==========================================T======x======                             │37          │0     │8     │143M1D6M   │  │
│=================================T==========xxxx============                         │37          │0     │23    │137M4D12M  │  │
│===============================================xxx============                       │37          │0     │35    │137M3D12M  │  │
│=========G================================T====T============                         │37          │0     │5     │149M       │  │
│===================================================C=====                            │37          │0     │49    │3S146M     │  │
│==========================================T=====================A==                  │37          │0     │32    │149M       │  │
│==========================================T========================                  │37          │0     │43    │149M       │  │
│======================G===================T===================G=====                 │37          │0     │14    │149M       │  │
│================================================A=============s(7)                   │37          │0     │59    │142M7S     │  │
│================A=================================A===s(15)                          │37          │0     │57    │134M15S    │  │
│=======================================================================              │37          │0     │7     │149M       │  │
│=============================T===========================================            │37          │0     │8     │149M       │  │
│==========================================================================           │37          │0     │20    │149M       │  │
│===============T==================================================̊=============      │37          │0     │16    │136M4I13M  │  │
│                                                                 ^AGTG               │            │      │      │           │  │
│==========================================T====================================      │37          │0     │12    │149M       │  │
│==========================================T=============xxx========================= │37          │0     │19    │124M3D25M  │  │
│==============================================T====================================  │37          │0     │30    │149M       │  │
│==========================================T========================================= │37          │0     │32    │149M       │  │
│=============C=======================================================================│37          │0     │56    │149M       │  │
│=====================================================================C=======s(10)   │37          │0     │50    │139M10S    │  │
│=======================A=============================================================│37          │0     │59    │149M       │  │
│========================================C============================================│37          │0     │20    │149M       │  │
│C===============================C===============̊=====================================│37          │0     │13    │100M4I49M  │  │
│                                               ^CGAC                                 │            │      │      │           │  │
│==========================================T==========================================│37          │0     │55    │149M       │  │
│==========================================T=======================================xxx│37          │0     │33    │123M4D26M  │  │
│==========================================T========================xxxx==============│37          │0     │13    │105M4D44M  │  │
│=============================C============T==========================================│37          │0     │34    │149M       │  │
│=====================G============================G==================================│37          │0     │60    │149M       │  │
│===========================C=====================C===================G===============│37          │0     │29    │20S129M    │  │
├─────────────────────────────────────────────────────────────────────────────────────┴─────────────────────────────────────────┤
│ LOCUS: demo:10000149 │ SPAN: 10000004-10000292 │                                                                              │
╭━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━╮
│WHERE basequal >= 37 AND mapq != 0   ← ACTIVE QUERY                                                                            │
│───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────│
│:where base != 'A'                   ← COMMAND LINE                                                                            │
│───────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────│
│OK!                                  ← RETURN MESSAGES                                                                         │
╰                                                                                                                               ╯
```

explanatory notes are in CAPITALS. 
`=` matches the reference, `x` is a deletion, and a ring over a base marks an insertion, displayed beneath at `^`.
**The real TUI has richer, better styling which copy-pasting does not preserve.**

Advantages:
- Immediately available in the terminal; no spinning up a genome browser instance or navigating a web UI.
- Easily installed, including on compute cluster nodes.
- Fast; no network IO, responsive UI.
- UI optimised for one job - inspecting pileup loci - rather than general-purpose genome browsing.
- Powerful SQL-backed query syntax for fast exploration.

This software is in a demo state and feedback is very much appreciated as I work towards a 1.0 release!

For CLI/TUI usage, command syntax, and query examples, generate a markdown manual specific to your version with `apb --manual > MANUAL.md`
(Check the helptext for exact instructions for manual generation).
You may also read the copy of [MANUAL.md](MANUAL.md) shipped with the repository without building,
but note that it may not exactly correspond to your version of the tool!

**`apb` displays all coordinate data in the TUI in 0-based half-open coordinates, matching the internal representation of htslib.
The sole exception is the locus argument when starting `apb` from the command line, which is 1-based to match samtools view,
and the representation of loci in VCF.**

## Install

You will need a terminal emulator with basic unicode support. I expect the TUI should display successfully on almost any modern-ish emulator.
`apb` has been confirmed to work in iTerm2, ghostty, vscode, Terminal.app (the macOS default), and from within tmux.
If you have issues with rendering please report them.

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
General suggestions regarding improvements to commands and navigation are also appreciated; I'm happy to make changes before a 1.0 release.

### Planned Features

- Multiple alignment pileups.
- More stats in the status bar; allele counts, VAF (when in variant driven mode), reference span complexity assessment (useful when
assessing artefactual variants).

### Speculative Features

These are items that I think might be useful and could implement, but am unlikely to do so without requests - if you see something you would like, please ask!

- Allow display of individual SAM aux tags as columns in the table pane. Tags are currently fully queryable, but they cannot be displayed.
- Allow providing a list or file of loci at the CLI, and jumping between them in the TUI.
  - Could also support VCF-driven locus browsing more specifically.
- Arbitrary locus-jumping from within TUI - e.g. `goto chr1:2500`.
  - Currently the view is fixed to a single locus specified at startup.
- Allow "saving" queries and returning to them within the same session, without having to type them out again.
- Optionally use unicode block characters to draw the quality string as a "sparkline" for reading at a glance,
  like so: ▁▂▃▄▅▆▇█▇▆▅▄▃▂▁ (example does not render well on github markdown viewer).
- Headless `count` mode, to get results for a query known at the CLI without dropping into the TUI.

### Non-feature Work

- Consistently assert invariants and preconditions in all frontend functions
- Still some work to be done on a consistent/better error handling policy; particularly on when to crash and how to gracefully do so.
- Backend code needs a cleanup pass in general and is overengineered around failure cases.
- Currently the demo mode database is fixed at compile time, meaning different builds will have different demo data.
  The demo data is generated in a parameterised manner so this isn't a big problem but it would be nice if a single
  demo database could be distributed within the src.

## Development

### Use of Hungarian Notation

**o_** - owned pointer, this scope must handle lifetime.  
**br_** - borrowed pointer, this scope must not affect lifetime.  
**k** - file-level constant used across multiple scopes. I don't use an underscore with this one.

I am almost certainly not using these reliably, but I do find them helpful.

### Dependencies

| Dependency | Version | Found via |
|---|---|---|
| sqlite3 | ≥3.38 | system, `pkg-config` |
| htslib | ≥1.17 | system, `pkg-config` (or `-DHTSLIB_INCLUDE_DIR`/`-DHTSLIB_LIBRARY`) |
| termbox2 | 605398fa | Vendored |
| plog | v1.1.10 | Vendored |
| argparse | v3.2 | Vendored |
| fmt | v12.2.0 | Vendored |
| Catch2 [optional] | v3.8.1 | CMake FetchContent |

### AI Usage

This repo has been developed by hand, with some use of AI tools for extraneous work like implementing githooks etc.
The design and implementation of all core types and logic are made by the maintainer.
Contributions are more than welcome, but would ideally follow this standard.

