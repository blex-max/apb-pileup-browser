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
