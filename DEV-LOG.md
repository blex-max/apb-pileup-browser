# Dev Log

## 03-03-26
(started devlog)
Moved from trying to pass around a global context instance
with subobjects describing modal contexts (e.g. pileup browser context)
to independent singleton context objects which can be retrieved by
type as needed. This was mostly inspired by realising that
getting a particular modal context subobject for a given app state (mode)
was a bit of a pain and would require std::variant or similar,
at least as far as I could see. The transition to the singleton pattern
was pleasantly easy and though it's not battle-tested at this point
it feels good so far.

## 05-03-26
singleton pattern continues to feel nice. I am focusing on trying to set
up as much as possible before even touching htslib, i.e. real data. Today
I've implemented basic drawing functions for the main pileup browser mode.
I'm pleased with the appearance so far. Next I want to introduce a debug
display which I'm sure will come in useful. Not much else to report really,
development today has felt gratifyingly smooth.

## 23-03-26
Implelemented slightly more featureful command input (backspace, really).
Implemented rendering of arbitrary debug info to display, which involved
road-testing the commands implementation a bit. All went reasonably well.
I think it is now time to move on to displaying pileup data.
Also did various restructing/reorg.

## 24-03-26
I realised that before displaying real data, there's further to go
with fake data! Figured out a modular and
configurable table display system yet, for displaying properties
of the pileup base. It is perhaps imperfect but quite functional!
I've also implemented basic user commands for showing/hiding
pileup/read properties at request. Still, before moving on
to real data I need to work out what happens when there is more
data than can be shown on screen at once.

# 25-04-26
I started working on the data model and plugging it in to the UI.
In the future I want to consider the ability to dump to TSV, so
decoupling is important.

# 06-05-26
I restructed the main rendering and event handling infrastructure
to localise all drawing calls to one place. Rather than each
element statefully handling drawing/redrawing of its features,
there is now a single draw call (subdivided into functions)
and other logic is separate.

## 02-07-26
*(the following is a Claude-generated summary of progress since the last
devlog entry, produced at the user's request against the working-tree
diff.)*
While starting on the `pileup_sort` console command in cmd.cpp, it became
clear I was slowly reinventing a subset of SQL. Discussed with a colleague
and decided to pivot: load the current pileup column straight into an
in-memory SQLite table and let the console run arbitrary SQL against it,
rather than hand-rolling filter/sort grammar. Since this is a *pileup*
browser rather than a full genome browser, at most a screen's worth of
reads (<10000, usually far fewer) is ever loaded at once, so an in-memory
table is cheap to rebuild on every cursor move. Sketched the shape of this
in sql-UML.puml (Pileup Engine -> Pileup2SQL -> SQLite table -> Query API
-> TUI/console).

Wrote the first concrete piece: `src/sql/schema.hpp`, a `reads` table
covering the 11 mandatory SAM fields (spec §1.4: QNAME, FLAG, RNAME, POS,
MAPQ, CIGAR, RNEXT, PNEXT, TLEN, SEQ, QUAL), aux tags (§1.5) serialized as
a JSON `tags` column so individual tags stay queryable via
`json_extract()` without a side table, plus the `bam_pileup1_t` fields
that are meaningful at a single reference position (qpos, indel, is_del,
is_head, is_tail, is_refskip, cigar_ind). Left out `level` (htslib's own
display-stacking value, not a read attribute), and `cd`/the reserved
padding bits, which are caller-owned scratch space nothing in this
codebase ever touches.

Kept the schema as a `constexpr std::string_view` in a header rather than
a loose `.sql` file, since there's no resource-embedding step in
CMakeLists.txt and this is a single self-contained binary — no reason to
add a runtime file read and a working-directory dependency for a DDL
string that never changes at runtime.

To verify: compiled a throwaway TU that just includes schema.hpp and
prints `kCreateReadsTable.size()`, under the project's actual warning
flags (`-std=c++23 -Wall -Wextra -Wpedantic`), to make sure the raw
string literal is well-formed and the header is self-contained. Didn't
yet wire it into the build or actually run the DDL through sqlite3 — that
happens once the loader code (accessors for QNAME/RNAME/MAPQ/CIGAR/
RNEXT/PNEXT/TLEN/tags, plus the INSERT loop) exists to actually populate
the table.

## 03-07-26
*(the following is a Claude-generated summary of progress since the last
devlog entry, produced at the user's request against the working-tree
diff.)*

Continued building out the SQL pivot described above: the pileup engine
and the pileup-to-SQL loading layer both now exist end-to-end and are
wired into the build, though the loading layer is still rough at the
edges.

**Pileup engine (`src/hts/pileup.{hpp,cpp}`)** — new, and largely
complete. `PileupBundle` owns an htslib `bam_plp_t` plus a non-owning
`PileupColumn` (a `vector<const bam_pileup1_t*>`) into it, and
`load_pileup()` drives `bam_plp64_auto` over a `sam_itr_queryi`-scoped
region to pull out the single pileup column at a requested
`{tid, pos}`, sorted by query start position. `PileupBundle` is
move-only (copy deleted, since it owns the htslib pileup iterator and
destroys it via `bam_plp_destroy` on drop). This is the layer that used
to be entangled with sorting/filtering console commands (per the
02-07-26 entry) — now it just produces a column; anything SQL-shaped
lives downstream.

**SQL loading layer (`src/sql/`)** — the schema DDL from the previous
entry is now actually run. `SqliteConn`/`SqliteErr` (`sql/types.hpp`)
wrap a raw `sqlite3*` with move-only ownership and an `operator
sqlite3*()` so call sites don't need an extra accessor layer.
`PileupDB` (`sql/create.hpp`) is a `SqliteConn` alias, deliberately *not*
bundled with pileup position metadata — noted in-code as a case of
avoiding premature coupling until there's a demonstrated need to key a
cache by position. `sql/create.cpp` implements `init()` (open + run
`CREATE TABLE`), `clear()` (`DELETE FROM reads`), and `insert_pileup()`
(single prepared `INSERT`, wrapped in an explicit `BEGIN`/`COMMIT`
transaction rather than one-row-per-autocommit).

`insert_pileup()` is the known-rough part: CIGAR is inserted as an empty
placeholder (stringification not written yet), `mtid` is inserted as an
empty string because the mate's reference name isn't reachable without
the header (which isn't threaded through to this function yet), and the
`tags` column is inserted as `NULL` pending aux-field → JSON
serialization. Error handling in the insert loop is stubbed (`// TODO:
ERR`) rather than actually returning the `SqliteErr` the function's
`VoidOrSqliteErr` return type promises. Left a comment flagging the
per-row `sqlite3_step` error path as unreviewed AI-generated code, to
come back to before trusting it.

**Accessors (`src/hts/accessors.{hpp,cpp}`)** — reshaped to feed the
insert loop directly: renamed/added thin wrappers over
`bam_pileup1_t`/`bam1_t` core fields (`start`, `mapq`, `mtid`, `mpos`,
`flag`, `base`, `qlen`) alongside the existing `seq()`, and added a new
`qual_ascii()` that renders the quality array as SAM-spec ASCII
(offset-33, or `*` for missing quality) instead of raw phred bytes —
needed since the `reads.qual` column is a text column mirroring the SAM
QUAL field. Return types on the small wrappers were loosened to `auto`
rather than spelling out htslib's underlying integer typedefs.

**Housekeeping** — `PileupPosition` moved out of `PileupContext.hpp` and
into `hts/pileup.hpp` where it's actually used. `AlnFile`'s
ctor/dtor/move-assignment bodies moved out of `accessors.cpp` into a new
`hts/types.cpp`, so `accessors.cpp` only holds field accessors and
`types.cpp` only holds `AlnFile` lifetime management. `src/table.{cpp,hpp}`
and `src/extb/extb-box.{cpp,hpp}` were deleted and reappear under
`src/extb/widgets/` (`table.{hpp,cpp}`, `extb-box.{hpp,cpp}`) — a
relocation rather than a rewrite, grouping UI widgets together now that
`extb-box` has SQL-console-facing siblings coming.

`CMakeLists.txt` now does `pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET
sqlite3)` and links `PkgConfig::DEPS`, alongside a small cleanup of the
htslib-discovery logic (dropped the redundant `_htslib_inc`/`_htslib_lib`
intermediate variables now that `HTSLIB_INCLUDE_DIR`/`HTSLIB_LIBRARY` are
used directly).

**Not yet done:** CIGAR stringification, mate reference-name lookup
(needs header plumbed into the insert path), aux tag → JSON
serialization, and real error propagation out of `insert_pileup()`. The
DDL has been run against real data via this loader for the first time,
but the row content is still partly placeholder.

## 10-07-26
*(the following is a Claude-generated summary of progress since the last
devlog entry, produced at the user's request against the commit history
and working-tree diff of tracked files — four commits, `pileup2db`
through `log to file`, all dated 09-07-26, plus a handful of uncommitted
tweaks.)*

This is the entry where the "not yet done" list from 03-07-26 gets
cleared, alongside a deliberate module reshuffle.

**The TUI is shelved, not deleted.** Every file belonging to the old
termbox2-based app (`app.*pp`, `cmd.*pp`, `ctx.hpp`, `input.*pp`,
`extb/`, `PileupContext.*pp`, `GlobalContext.hpp`, `tb2_impl.cpp`) moved
as a block, unmodified, from `src/` into `src/app-tmp/` and dropped out
of the CMake executable's source list — `CMakeLists.txt`'s
`add_executable(pileup-browser ...)` now lists just
`core/PileupDB.cpp`, `main.cpp`, `cli.cpp`, `demo.cpp`. termbox2 is still
fetched and linked but nothing compiled currently calls into it. This
reads as the pivot decided on 02-07-26 (pileup browser → SQL backend)
reaching its logical endpoint: rather than keep threading SQL through
the existing TUI incrementally, the TUI is parked wholesale while the
backend is finished and proven out standalone.

**`src/sql/` and `src/hts/` collapsed into `src/core/`.** Where the
03-07-26 layout had the pileup engine, accessors, and SQL loading as
separate directories, everything now lives in `core/`: `hts_types.hpp`
(`AlnFile`, `PileupPosition`, `GenomeSpan` — moved in from their old
homes), `sql_types.hpp` (`SqliteConn`, the templated
`SqliteStmt<Tag>` wrapper), `sql.hpp` (the raw DDL/DML strings),
`err.hpp` (new unified error type, see below), and `PileupDB.{hpp,cpp}`
(the actual pileup→SQL logic). The old standalone `pileup.{hpp,cpp}`
engine (a separate `PileupBundle`/`load_pileup()` abstraction from
03-07-26) is gone; pileup iteration now lives inline in `PileupDB.cpp`
as `PreparedPileup`/`prepare_pileup()`, built directly around a
`PileupCapture` (`uo_fh` borrowed, `o_it` owned) and a `pileup_func`
callback driving `sam_itr_next`.

**`insert_pileup()` is now real, not a rough draft.** Against the
03-07-26 placeholder list:
- CIGAR is stringified properly (`bam_cigar_oplen`/`bam_cigar_opchr` per
  op), and `rend` is computed from it via `bam_cigar2rlen` rather than
  left as an empty string.
- Mate reference name is resolved via `sam_hdr_tid2name` against
  `aln.o_hdr`, threaded down into the per-row loop rather than needing a
  separate plumbing pass — `'='` when the mate shares the read's own
  `tid` (per SAM RNEXT convention), the looked-up name otherwise, empty
  (→ SQL `NULL`) when the read has no mate.
- Aux tags are fully serialized to JSON (`aux1_to_json`, new): walks
  `bam_aux_first`/`bam_aux_next`, calls `sam_format_aux1` per tag, and
  handles both `'B'` (numeric array → JSON array) and scalar types
  (`'A'`/`'Z'`/`'H'` as JSON strings, everything else as a bare JSON
  number). A dedicated `append_json_escaped()` escapes `"`/`\`/control
  characters when writing string-valued tags, since SAM's `A`/`Z` aux
  values are allowed to contain raw `"` and `\` unescaped — without it,
  a legal tag value could produce invalid JSON and trip the
  `reads.tags CHECK(json_valid(tags))` constraint from the 02-07-26
  schema.
- Error propagation is real `VoidOrErr`/`std::expected` throughout, not
  a `// TODO: ERR` stub: failures roll back the open transaction and, if
  the rollback itself fails, that failure's message is appended to the
  original error rather than swallowed.

**New: `dump_to_disk()`.** Copies the in-memory db out to a file path
via sqlite3's online backup API (`sqlite3_backup_init`/`_step`/`_finish`
against a freshly-opened destination handle), under the documented
assumption that the source db is quiescent for the copy's duration.
Wired up behind a new `--dump PATH` CLI flag — this is what actually
got exercised end-to-end against real data in the `e2e run of
pileup->sqlite functional on real data!` commit.

**New: `cli.cpp`/`cli.hpp`**, built on `argparse` (new dependency,
v3.2). `--demo` (synthetic data, no alignment file needed), `--dump
PATH`, `--log PATH`, and positional `SAM`/`region` arguments. Region
parsing uses `hts_parse_region` against the loaded header; opening the
alignment file (`hts_open`/`sam_hdr_read`/`sam_index_load` into an
`AlnFile`) happens eagerly during arg parsing rather than later, with a
noted TODO that it'd be cleaner to just store paths here and open files
once the full argument set is validated.

**New: `demo.cpp`.** `insert_demo_data()` populates the `reads` table
with synthetic random-base-sequence reads at random offsets, going
straight through `bind_pileup_fields()` without touching htslib or the
pileup engine at all — driven by the `--demo` CLI flag.

**Logging now goes to a file.** `plog::init` takes a rolling file sink
(`--log PATH`, 10MB cap) instead of console-only output (`log to file`
commit).

**Unified error type.** `core/err.hpp` replaces the rougher, per-module
error handling from 03-07-26 with one `Err{kind, src, code, msg}` (plus
`make_htslib_err`/`make_sqlite3_err`/`make_cli_err`/`make_internal_err`
helpers) used consistently across `insert_pileup`, `dump_to_disk`,
`fill_fields`, and `init_cli`. `ErrKind` currently has only `fatal`, with
a comment flagging that recoverable errors may be distinguished later.

**Test scaffold.** `CMakeLists.txt` gained a `MAKE_TEST` option that,
when on, fetches Catch2 v3.8.1 and wires up a `pileup-browser-tests`
target via `catch_discover_tests`. It also bumped the sqlite3
dependency to `>=3.38`, noted as the version where `json_valid()` (used
by the `reads.tags` CHECK constraint) became built-in rather than
requiring the separate JSON1 extension.

## 14-07-26
*(the following is a Claude-generated summary of progress since the last
devlog entry, produced at the user's request against the commit history
— four commits, `minor cleanup` through `clang-format change`, dated
10-07-26 and 13-07-26.)*

The db schema gained a `sample` table and a `loci` table (`sample_id`
FK, `contig`, `pos`), both `ON DELETE CASCADE` with covering indexes;
`reads` now carries matching `sample_id`/`loci_id` FKs, and `PRAGMA
foreign_keys = ON` is set at db-open. New `insert_sample()`/
`insert_loci()` populate the two tables, and `insert_pileup()` now
takes an `alnId` to tie its reads back to a sample. Implementation-only
types (`PileupFields`, statement-prep helpers, etc.) moved out of
`PileupDB.hpp` into a new `PileupDB_internal.hpp`, and the three
per-statement `prepare_insert_*` functions collapsed into one templated
`prepare<StmtT>(db)`.

The CLI moved from flat `--demo`/positional args to `sam`/`db`/`demo`
subcommands. `parse_args()` (renamed from `init_cli`) now returns a
`std::variant<AlnModeArgs, DbModeArgs, DemoModeArgs>`, and `main.cpp`
is reduced to a `std::visit` over new `run_mode()` overloads in
`src/subcommands.{hpp,cpp}`. The `db` mode's `run_mode` is currently a
stub; the other two still end in a `// load frontend` comment, not yet
wired up.

`.clang-format` and a `.githooks/pre-commit` hook (`git clang-format
--staged`) were added, and the whole tree reformatted to match
(including a follow-up tweak to `SpaceBeforeParens`).

