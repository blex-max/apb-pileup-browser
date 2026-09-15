<!-- GENERATED FILE — DO NOT EDIT.
     Produced by `apb --dump-manual`; edit the manual content
     in src/app/manual.cpp instead. -->

# `apb` Manual

## Overview

Given an alignment file and a genomic locus `apb` builds the pileup at that position and loads the reads into a fast, queryable
database structure. The TUI then renders the reads as aligned at the pileup position, displays user-selected
data for each read (e.g. mapping quality, leftmost alignment position, etc.) and provides a command line at which you can enter commands to
query the reads or change the display. The display is navigated using simple arrow-key navigation.

## CLI Usage

There are three modal subcommands available when starting `apb`.

The CLI is in a demo state - the subcommand approach might not be long term.

`apb sam <alignment-file> <locus> [--ref reference.fasta] [--dump out.db]`  

`apb db <dumped.db>`  

`apb demo [--dump out.db]`  

`sam` opens a live alignment file (SAM/BAM/CRAM) at a locus (`chr1:12345`) and launches the TUI. When specifying a locus, it is in the form
`contig:coordinate` - only a single coordinate needs to be provided, rather than a length-1 range as in many `samtools` commands. **The
locus coordinate is 1-based**, as `samtools` CLI commands. If a reference is provided, the reference will be shown with the aligned reads
and the read view will be enriched indicating differences from reference.

`db` reopens a database file previously produced by `--dump` (or the in-TUI `dump` command).

`demo` runs against synthetic data, no alignment file required. Good for a first look at the tool, but note that since the data is
artifically generated not everything works quite as it should - some fields are not properly set in the database.

`apb --log <path.txt>` (`--log` comes before the subcommand) enables debug logging. Valuable to turn on during this early development stage
in case any crashes are encountered!

## TUI Usage

Note that this is all subject to change pending user feedback.

### Basic Navigation

Normal typing goes directly to the command line. `Enter` dispatches the contents as a command.

**Navigation Keys**:

**Browser pane**
- `↑` / `↓` scroll the alignment view by one row.
- `PgUp` / `PgDn` scroll by a full page.

**Command line**
- `Shift+↑` / `Shift+↓` step through command history.
- `←` / `→` move the cursor; `Ctrl-A` / `Ctrl-E` jump to start/end; `Alt+←` / `Alt+→` (or `Alt+b` / `Alt+f`) jump by word.
- `Backspace` deletes a character; `Alt+Backspace` clears the whole line.

`Ctrl-C` clears the command line if any input is present, and exits the program otherwise.

### Command Reference

| Command | Aliases | Usage | Description |
|---|---|---|---|
| `help` | `h`, `?` | `[(nav\|cmd)]` | Show help for given topic, or general help with no args. |
| `quit` | `q`, `exit` |  | Exit the browser. |
| `where` | `wh` | `<clause>` | Start a new WHERE clause, overwriting any existing clause. |
| `and` |  | `<clause>` | Extend current WHERE clause with an AND condition. |
| `or` |  | `<clause>` | Extend the query with an OR condition. |
| `back` | `bk` |  | Drop the most recently added condition from the query WHERE clause, or clear if only a single condition is present. |
| `clear-where` | `cw` |  | clear WHERE clause, retaining ORDER BY. |
| `order-by` | `order`, `ob` | `<clause>` | Sort reads by ORDER BY expression. |
| `clear` | `cl` |  | Clear current query. |
| `dump` |  | `<path>` | Write the in-memory database to a file. Takes a single path. The current query is not preserved. |
| `pane` |  | `[aln\|table]` | show/hide either of the alignment or table panes, or reset to default with no args. |
| `track` |  | `[(qual\|ins)...] - nargs: 0 - 2` | toggle display of additional tracks in browser alignment pane, or reset to default with no args. |
| `col` |  | `<field-name>...` | Toggle display of read data fields to the tabular display. |
| `count` | `ct` | `[clause]` | Count reads matching current query. If provided, the optional clause will be AND-concatenated onto the existing WHERE clause for the count query. If no WHERE clause is present, the optional clause will be used as the count WHERE clause alone. |

Every line typed at the command line is dispatched like `<command> [args]`.

### Querying the Pileup

The `apb` query commands provide a simple wrapper around (SQLite-flavoured) SQL. Whereas a more generic SQL REPL might be structured around
dispatching known, predetermined queries, `apb` aims to support stepwise pattern discovery. The active query (a WHERE clause plus an ORDER
BY) persists across commands and may be extended a piece at a time (using `where`, `and`/`or`, and `order`). By example, a session might
look like: check which reads carry a non-reference base, filter those by a base quality threshold, decide that's not informative and undo
, filter by mapping quality instead, and so on. See [Table Reference](#Table-Reference) for the full list of queryable columns.

#### Query Walkthrough

Imagine a putative variant locus under manual inspection. Starting broad, we can filter the view to show only reads with a non-reference
base at the pileup position. Assume `G` is the reference base:
```
where base != 'G'
```
We then filter reads with any of flag bits 9/10/11 set (supplementary, duplicate, or QC fail):
```
and (flag & 3584) = 0
```
Note that flag is a bitmask - see [here](https://www.w3schools.com/programming/prog_operators_bitwise.php) for a quick introduction to
bitmask syntax. SQLite supports the bitwise and, or, xor and not. [This webpage](https://broadinstitute.github.io/picard/explain-flags.html)
from the Broad Institute is very useful for finding the corresponding integer given a set of SAM bits.

Back to our query. Finally, let's sort to see the weakest evidence first:
```
order basequal ASC
```
ASC is simply SQLite's shorthand for ascending. DESC is the alternative. If you omit the sort direction term, ASC is the default. Also note
that you can order by multiple keys, e.g. `order basequal DESC, rstart ASC` would sort by basequal in descending order, and where basequal
is equal, alignment start position will be used as a secondary key.

The parser incrementally wraps these commands into a complete SQL statement.
Note that you could also write the full command as a single statment:
```
where base != 'G' AND (flag & 3584) = 0 ORDER BY basequal ASC
```
The two styles are equally supported; in both cases, you can continue to add on further clauses with `and` and `or` as you like.
You can remove clauses added in a piecewise manner with the `back` command.

If you want a **count** rather than a filtered view, `count [clause]` answers without disturbing the active query. For example, `count mapq
< 20` tells you how many low-mapping-quality reads are without chainging the view.

#### Further Examples

Beyond plain comparisons, SQLite's full function library is available. A few examples:

##### Cigar querying

`cigar` is a plain string, so text matching works directly on it. E.g. to search for reads containing any insertion:
```
where instr(cigar, 'I')
```
or, if you prefer
```
where cigar like '%I%'
```

##### Aux tags

`tags` is a JSON blob of the read's aux tags — extract tags with `->>`:
```
where tags ->> '$.NM' > 2
```
or checking a read group:
```
where tags ->> '$.RG' = 'sample1'
```
A read with no aux tags, or missing that specific tag, comes back as SQL `NULL` rather than an error, so `where tags ->> '$.RG' is null`
finds reads missing that tag.

##### Motifs at the query position

This is a slightly more advanced example. `qpos` is the 0-based offset into `seq` for the base at the pileup position, and SQLite's
`substr()` is 1-based, so to search for the 4-mer `GATC` starting at the query position:
```
where substr(seq, qpos + 1, 4) = 'GATC'
```
`LIKE` (`_`/`%`) and `GLOB` (`?`/`*`/`[ACG]`) both work as wildcards — `GLOB`'s character classes are useful for ambiguity. To search for
two possible trinucleotide motifs at the query position:
```
where substr(seq, qpos + 1, 3) glob 'A[CG]T'
```
You can also search for motifs within a window of the `seq` string. This command searches for `G