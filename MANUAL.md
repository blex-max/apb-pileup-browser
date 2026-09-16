
# `apb` Manual

**This manual is generated directly from the `apb` binary. Please use `apb --dump-manual` to ensure you are
reading the information appropriate to your version of the tool.**

## Overview

Given an alignment file and a genomic locus `apb` builds the pileup at that position and loads the reads into a fast, queryable
database structure. The TUI then renders the reads as aligned at the pileup position, displays user-selected
data for each read (e.g. mapping quality, leftmost alignment position, etc.) and provides a command line at which you can enter commands to
query the reads or change the display. The display is navigated using simple arrow-key navigation.

**`apb` displays all coordinate data in the TUI in 0-based half-open coordinates, matching the internal representation of htslib.
The sole exception is the locus argument when starting `apb` in locus mode from the command line, which is 1-based to match
samtools view, and the representation of loci in VCF.**

## CLI Usage

```
usage: apb [options] MODE [FILE] [LOCI] [REF]

 apb is an terminal-based genome browser designed for viewing
 and querying pileup loci. It features a REPL-like command
 line and simple SQL-based query syntax.

modes:
  locus  FILE LOCUS [REF]   view a single locus
                            FILE   alignment file (sam/bam/cram)
                            LOCUS  genomic locus, e.g. chr1:12345
                            REF    reference fasta (optional)
  db     DB                 load from a dumped db
                            DB     path to db dump
  demo                      view demo data

options:
  -h, --help          show this help message and exit
  -v, --version       print version information and exit
  --dump PATH         convert pileup to sqlite3 database, dump to disk, and exit
                      (invalid in db mode)
  --dump-manual PATH  write the apb manual to PATH and exit
  --log PATH          log debug output to file

 IMPORTANT:
  apb displays all coordinate data in 0-based half-open
  coordinates, matching the internal representation of htslib.
  The sole exception is the locus argument to locus mode,
  which is 1-based to match samtools, and the
  representation of loci in VCF.


 See README.md for project background, or MANUAL.md for
 usage (or use the in-app help: type ? and press enter in
 the TUI). If you don't have the manual, write it to disk
 with `apb --dump-manual PATH`.

 In the TUI, type q and press enter or press Ctrl-C
 twice to quit.

```

`locus` opens a live alignment file (SAM/BAM/CRAM) at a locus (`chr1:12345`) and launches the TUI. A locus is specified in the
form `contig:coordinate` - only a single coordinate needs to be provided, rather than a length-1 range as in many `samtools` commands.
**The locus coordinate is 1-based**, as `samtools` CLI commands. If a reference is provided, the reference will be shown with the aligned
reads and the alignment view will be enriched indicating differences from reference.

`db` reopens a database file previously produced by `--dump` (or the in-TUI `dump` command).

`demo` runs against synthetic data, no alignment file required. Good for a first look at the tool, but note that since the data is
artifically generated not everything works quite as it should - some fields are not properly set in the database.

`apb --log <path.txt>` enables debug logging. Valuable to turn on during this early development stage in case any crashes are encountered!

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
| `pane` | `p` | `[aln\|table]` | show/hide either of the alignment or table panes, or reset to default with no args. |
| `track` | `t` | `[(qual\|ins)...] - nargs: 0 - 2` | toggle display of additional tracks in browser alignment pane, or reset to default with no args. |
| `col` | `c` | `<field-name>...` | Toggle display of read data fields to the tabular display. |
| `count` | `ct` | `[clause]` | Count reads matching current query. If provided, the optional clause will be AND-concatenated onto the existing WHERE clause for the count query. If no WHERE clause is present, the optional clause will be used as the count WHERE clause alone. |

Every command submitted at the command line is interpreted like `<command> [args]`.

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
Note that you could also write the full command as a single statment via the `where` command:
```
where base != 'G' AND (flag & 3584) = 0 ORDER BY basequal ASC
```
The two styles are equally supported; in both cases, you can continue to add on further clauses with `and` and `or` as you like.
You can remove clauses added in a piecewise manner with the `back` command.

If you want a **count** rather than a filtered view, `count [clause]` answers without disturbing the active query. For example, `count mapq
< 20` tells you how many low-mapping-quality reads there are in the current query without changing the view.

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

`tags` is a JSON blob of the read's aux tags. Tags may be extracted with `->>`:
```
where tags ->> '$.NM' > 2
```
or checking a read group:
```
where tags ->> '$.RG' = 'sample1'
```
A read with no aux tags, or missing that specific tag, returns SQL `NULL`, so `where tags ->> '$.RG' is null`
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
You can also search for motifs within a window of the `seq` string. This command searches for `GATC` within the first 10 bases of the read:
```
where instr(substr(seq, 1, 10), 'GATC') > 0
```

Any and all of these approaches may be combined, and more is possible. See [SQLite's expression/function
reference](https://sqlite.org/lang_expr.html).

#### Table Reference

For each read, the database stores the following information. All columns are queryable in `where`/`and`/`or`/`order` commands. If the
content of a column is not clearly displayed by the alignment view, the column can be displayed alongside the reads in tabular format.

| Column | Meaning |
|---|---|
| `qname` | read/template name |
| `flag` | SAM bitwise FLAG |
| `rstart` | 0-based leftmost mapping position |
| `rend` | 0-based rightmost mapping position |
| `mapq` | mapping quality |
| `basequal` | Phred base quality at the pileup position |
| `qpos` | 0-based offset into `seq`/`qual` for the pileup locus position |
| `cigar` | CIGAR string |
| `mtid` | reference name of the mate/next read |
| `mstart` | mate/next read's leftmost mapping position |
| `tags` | aux tags as JSON; able to be individually queried |
| `base` | the read's base at the pileup position |
| `indel` | indel length to the next mapped base in the read (0 none, >0 insertion, <0 deletion) |
| `is_del` | 1 if this position is a deletion |
| `is_head` | 1 if this is the read's first aligned base |
| `is_tail` | 1 if this is the read's last aligned base |
| `is_refskip` | 1 if this position is a reference skip |
| `seq` | the read's sequence string |
| `qual` | the read's ASCII quality string |
| `ncig` | number of CIGAR operations in the read |

`indel` might require some explanation. Essentially, if the base at the pileup position is followed by an indel, then `indel` will contain
the size of that indel event. A deletion is represented by a negative size (bases lost), and an insertion is represented by a positive size
(bases gained).

The first eleven (`qname` through `tags`) can also be displayed in tabular format; see [Command Reference](#Command Reference) for details
on showing and hiding particular columns.

For advanced users, note that most of these map directly onto fields in htslib's `bam_pileup1_t` and `bam1_t` structs.

### A Word on `dump` Functionality

A dump is a small, self-contained sqlite3 file with just the reads at this one locus. Picking a session back up later with `apb db` is one
reason to use it; a few others:

- Full SQL - `sqlite3 my.db` allows for more complex analysis if needed (`GROUP BY`, aggregates, e