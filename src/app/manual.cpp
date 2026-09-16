#include "manual.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <cstddef>
#include <string>
#include <string_view>

#include "app/cmd.hpp"
#include "app/text_blocks.hpp"


static constexpr std::string_view sh_manualPre = R"txt(
apb Manual - A Pileup Browser

**IMPORTANT**:
The apb manual is generated directly from the binary. Please check the helptext for instructions as to how to generate the manual for your version of the tool.

1) Overview

Given an alignment file and a genomic locus apb builds the pileup at that position and loads the reads into a fast, queryable database structure. The TUI then renders the reads as aligned at the pileup position, displays user-selected data for each read (e.g. mapping quality, leftmost alignment position, etc.), and provides a command line at which you can enter commands to query the reads or change the display. The display and command line can be navigated via simple arrow key navigation.

**IMPORTANT**:
apb displays all coordinate data in the TUI in 0-based half-open coordinates, matching the internal representation of htslib. The sole exception is the locus argument when starting apb in locus mode from the command line, which is 1-based to match samtools view, and the representation of loci in VCF.

2) CLI Usage

See the helptext from `apb --help` for the canonical guide to the CLI. Some additional context is given here.

`apb locus ...` opens an alignment file (SAM/BAM/CRAM) at a locus (`chr1:12345`), loads the pileup, and launches the TUI. A locus is specified in the form `contig:coordinate` - only a single coordinate needs to be provided, rather than a length-1 range as in many `samtools` commands. If a reference is provided, the reference will be shown and the alignment view will be enriched indicating divergence from the reference in the displayed reads.

**IMPORTANT**:
The locus coordinate is 1-based, as `samtools` CLI commands and the VCF spec. This is in contrast to the display inside the browser, which is 0 based.

`apb demo` runs against synthetic data, no alignment file required. Good for a first look at the tool, but note that since the data is artifically generated the data is unrealistic and relatively uniform, and therefore it is not the best test of apb.

`apb db ...` reopens a database file previously produced by `--dump` (or the in-TUI `dump` command).

`apb --log <path.txt>` enables debug logging. Valuable to turn on during this early development stage in case any crashes are encountered!

3) TUI Usage

Note that this is all subject to change pending user feedback.

3.1) Basic Navigation

The alignment view (i.e. the reads) are scrolled up/down with the arrow keys. Mouse scroll may also work if your terminal is configured to send mouse scrolling as arrow keys, which is reasonably common. Normal typing goes directly to the command line at the bottom of the TUI. `Enter` dispatches the contents as a command.

Navigation Keys:

)txt";

static constexpr std::string_view sh_manualMid = R"txt(

Ctrl-C clears the command line if any input is present, and exits the program otherwise.

3.2) Command Reference

Every command submitted at the command line is interpreted like `<command> [args]`. Shorthand aliases are provided for many commands for quick use.

)txt";

static constexpr std::string_view sh_manualPost = R"txt(

3.3) Querying the Pileup

The apb query commands provide a simple wrapper around (SQLite-flavoured) SQL. Whereas a more generic SQL REPL might be structured around dispatching known, predetermined queries, apb aims to support stepwise pattern discovery. The active query (a WHERE clause plus an ORDER BY) persists across commands and may be extended a piece at a time (using `where`, `and`/`or`, and `order`). By example, a session might look like the following: check which reads carry a non-reference base, filter those by a base quality threshold, decide that's uninformative and undo, filter by mapping quality instead, and so on. See Section 3.4 (Table Reference) for the full list of queryable columns.

3.3.1) Query Walkthrough

Imagine a putative variant locus under manual inspection. Starting broad, we can filter the view to show only reads with a non-reference base at the pileup position. Assume `G` is the reference base:
```
where base != 'G'
```
We then filter reads with any of flag bits 9/10/11 set (supplementary, duplicate, or QC fail):
```
and (flag & 3584) = 0
```
Note that flag is a bitmask - see https://www.w3schools.com/programming/prog_operators_bitwise.php for a quick introduction to bitmask syntax. SQLite supports the bitwise and, or, xor and not. The webpage https://broadinstitute.github.io/picard/explain-flags.html from the Broad Institute is very useful for finding the corresponding integer given a set of SAM bits.

Back to our query. Finally, let's sort to see the weakest evidence first:
```
order basequal ASC
```
ASC is simply SQLite's shorthand for ascending. DESC is the alternative. If you omit the sort direction term, ASC is the default. Also note that you can order by multiple keys, e.g. `order basequal DESC, rstart ASC` would sort by basequal in descending order, and where basequal is equal, alignment start position will be used as a secondary key.

The parser incrementally wraps these commands into a complete SQL statement. Note that you could also write the full command as a single statment via the `where` command:
```
where base != 'G' AND (flag & 3584) = 0 ORDER BY basequal ASC
```
The two styles are equally supported; in both cases, you can continue to add on further clauses with `and` and `or` as you like. You can remove clauses added in a piecewise manner with the `back` command.

If you want a count rather than a filtered view, `count [clause]` answers without disturbing the active query. For example, `count mapq < 20` tells you how many low-mapping-quality reads there are in the current query without changing the view.

3.3.2) Further Examples

Beyond plain comparisons, SQLite's full function library is available. A few examples:

CIGAR QUERYING:

`cigar` is a plain string, so text matching works directly on it. E.g. to search for reads containing any insertion:
```
where instr(cigar, 'I')
```
or, if you prefer
```
where cigar like '%I%'
```

AUX TAGS:

`tags` is a JSON blob of the read's aux tags. Tags may be extracted with `->>`:
```
where tags ->> '$.NM' > 2
```
or checking a read group:
```
where tags ->> '$.RG' = 'sample1'
```
A read with no aux tags, or missing that specific tag, returns SQL `NULL`, so `where tags ->> '$.RG' is null` finds reads missing that tag.

MOTIFS AT THE QUERY POSITION:

This is a slightly more advanced example. `qpos` is the 0-based offset into `seq` for the base at the pileup position, and SQLite's `substr()` is 1-based, so to search for the 4-mer `GATC` starting at the query position:
```
where substr(seq, qpos + 1, 4) = 'GATC'
```
`LIKE` (`_`/`%`) and `GLOB` (`?`/`*`/`[ACG]`) both work as wildcards — `GLOB`'s character classes are useful for ambiguity. To search for two possible trinucleotide motifs at the query position:
```
where substr(seq, qpos + 1, 3) glob 'A[CG]T'
```
You can also search for motifs within a window of the `seq` string. This command searches for `GATC` within the first 10 bases of the read:
```
where instr(substr(seq, 1, 10), 'GATC') > 0
```

Any and all of these approaches may be combined, and more is possible. See SQLite's expression/function reference - https://sqlite.org/lang_expr.html.

3.4) Table Reference

For each read, the database stores the columns detailed below. All columns are queryable in `where`/`and`/`or`/`order` commands. If the content of a column is not clearly displayed by the alignment view, the column can be displayed alongside the reads in tabular format.

 TABLE REFERENCE
  `qname`:
      read/template name
  `flag`:
      SAM bitwise FLAG
  `rstart`:
      0-based leftmost mapping position
  `rend`:
      0-based rightmost mapping position
  `mapq`:
      mapping quality
  `basequal`:
      Phred base quality at the pileup position
  `qpos`:
      0-based offset into `seq`/`qual` for the pileup locus position
  `cigar`:
      CIGAR string
  `mtid`:
      reference name of the mate/next read
  `mstart`:
      mate/next read's leftmost mapping position
  `tags`:
      aux tags as JSON; able to be individually queried
  `base`:
      the read's base at the pileup position
  `indel`:
      indel length to the next mapped base in the read (0 none, >0 insertion, <0 deletion)
  `is_del`:
      1 if this position is a deletion
  `is_head`:
      1 if this is the read's first aligned base
  `is_tail`:
      1 if this is the read's last aligned base
  `is_refskip`:
      1 if this position is a reference skip
  `seq`:
      the read's sequence string
  `qual`:
      the read's ASCII quality string
  `ncig`:
      number of CIGAR operations in the read

`indel` might require some explanation. Essentially, if the base at the pileup position is followed by an indel, then `indel` will contain the size of that indel event. A deletion is represented by a negative size (bases lost), and an insertion is represented by a positive size (bases gained).

The first eleven (`qname` through `tags`) can also be displayed in tabular format; see Section 3.2 (Command Reference) for details on showing and hiding particular columns.

For advanced users, note that most of these map directly onto fields in htslib's `bam_pileup1_t` and `bam1_t` structs.

5) A Word on `dump` Functionality

A dump is a small, self-contained sqlite3 file with just the reads at the specified locus. A few use cases:

- Full SQL - `sqlite3 my.db` allows for more complex analysis if needed (`GROUP BY`, aggregates, etc.).
- Downstream use - A dump is a normal sqlite3 file, so anything with a sqlite driver can read it.
- Sharing - send a colleague exactly the reads you're looking at, at a fraction of the size, without them needing the original BAM/CRAM, reference genome, or even apb if they're happy just to use `sqlite3`.
- Debugging (for developers) - a dump is a stable snapshot of exactly what got loaded, inspectable without the original alignment file or TUI. Mostly relevant if you're developing apb itself rather than just using it.

6) A Word on Indexing Systems

htslib/samtools/bcftools, and by extension all alignment and VCF data, mix 3 (3!!) coordinate systems. This can be tricky to navigate.

apb uses 0-based half-open coordinates throughout, except for the locus argument when starting apb from the command line in locus mode, which is 1-based. A 1-based locus argument has the advantage of being identical to the VCF `POS` field per the VCF specification, and to `samtools` commands e.g. `samtools view ...`. However, `htslib`'s internal alignment representation format is 0-based, so it is more natural (and less bug-prone) to display the alignment information as 0-based. This is an inevitable UX compromise.

---

See the README or the project GitHub (https://github.com/blex-max/apb-pileup-browser) for installation instructions, the project roadmap, and other background.
)txt";

std::string_view get_manual()
{
  static const std::string sh_manual = fmt::format (
      "{}{}{}{}{}", sh_manualPre, fmt::join (sh_navBlock, "\n"),
      sh_manualMid, fmt::join (build_cmd_ref_table(), "\n"),
      sh_manualPost
  );
  return sh_manual;
}
