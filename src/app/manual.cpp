#include "manual.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include "app/cmd.hpp"

static constexpr size_t max_line_len (std::string_view text)
{
  size_t maxLen = 0;
  size_t cur = 0;
  for (char c : text) {
    if (c == '\n') {
      maxLen = std::max (maxLen, cur);
      cur = 0;
    }
    else {
      ++cur;
    }
  }
  return std::max (maxLen, cur);
}

static std::string cmd_usage_args (const CmdView& cmd)
{
  if (cmd.usage.size() <= cmd.call.size()) {
    return {};
  }
  return std::string (cmd.usage.substr (cmd.call.size() + 1));
}

// GFM table cells can't contain a raw pipe.
static std::string md_escape_pipes (std::string_view text)
{
  std::string out;
  out.reserve (text.size());
  for (char c : text) {
    if (c == '|') {
      out += "\\|";
    }
    else {
      out += c;
    }
  }
  return out;
}

static std::string build_cmd_ref_markdown_table()
{
  std::string out{
      "| Command | Aliases | Usage | Description |\n"
      "|---|---|---|---|\n"
  };
  for (const auto* cmd : get_cmd_registry()) {
    const auto args = cmd_usage_args (*cmd);
    out += fmt::format (
        "| `{}` | {} | {} | {} |\n", cmd->call,
        cmd->alias.empty()
            ? std::string{}
            : fmt::format (
                  "`{}`", fmt::join (cmd->alias, "`, `")
              ),
        args.empty()
            ? std::string{}
            : fmt::format ("`{}`", md_escape_pipes (args)),
        cmd->desc
    );
  }
  return out;
}

static constexpr std::string_view sh_manualPre = R"md(
# `apb` Manual

**This manual is generated directly from the `apb` source. Please use `apb --dump-manual` to ensure you are
reading the information appropriate to your version of the tool.**

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

)md";
static_assert (
    max_line_len (sh_manualPre) <= 140, "manual line too long"
);

static constexpr std::string_view sh_manualPost = R"md(
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
| `base` | the read's base at the pileup position |
| `basequal` | Phred base quality at the pileup position |
| `qpos` | 0-based offset into `seq`/`qual` for the pileup locus position |
| `cigar` | CIGAR string |
| `mtid` | reference name of the mate/next read |
| `mstart` | mate/next read's leftmost mapping position |
| `tags` | aux tags as JSON; able to be individually queried |
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
(bases gained). I need to confirm the behaviour of the field when the pileup base itself is deleted.

The first twelve (`qname` through `tags`) can also be displayed in the info pane; see [Command Reference](#Command Reference) for details.

For advanced users, note that most of these map directly onto fields in htslib's `bam_pileup1_t` and `bam1_t` structs.

### A Word on `dump` Functionality

A dump is a small, self-contained sqlite3 file with just the reads at this one locus. Picking a session back up later with `apb db` is one
reason to use it; a few others:

- Full SQL — `sqlite3 my.db` gets you everything the in-TUI REPL deliberately doesn't: `GROUP BY`, aggregates, etc. Allows for more
complex analysis if needed.
- Downstream use — it's a normal sqlite3 file, so anything with a sqlite driver can read it.
- Sharing — send a colleague exactly the reads you're looking at, at a fraction of the size, without them needing the original BAM/CRAM,
reference genome, or even `apb` if they're happy just to use `sqlite3`.
- Debugging (for developers) — a stable snapshot of exactly what got loaded, inspectable without the original alignment file or the TUI.
Mostly relevant if you're developing `apb` itself, rather than just using it.

### A Word on Indexing Systems

`htslib`/`samtools`/`bcftools`, and by extension all alignment and VCF data, mix 3 (3!!) coordinate systems. This can be tricky to navigate.

**`apb` uses 0-based half-open coordinates throughout, except for the locus argument when starting `apb` from the command line, which is
1-based**. A 1-based locus argument has the advantage of being identical to the VCF `POS` field per the VCF specification, and to `samtools`
commands e.g. `samtools view ...`. However, `htslib`'s internal alignment representation format is 0-based, so it is more natural (and less
bug-prone) to display the alignment information as 0-based. This is an inevitable UX compromise - feedback is appreciated.

---

See the [README](https://github.com/blex-max/apb-pileup-browser) for installation instructions, the project roadmap, and other
background.
)md";
static_assert (
    max_line_len (sh_manualPost) <= 140, "manual line too long"
);

std::string_view get_manual()
{
  static const std::string sh_manual = fmt::format (
      "{}{}{}", sh_manualPre, build_cmd_ref_markdown_table(),
      sh_manualPost
  );
  return sh_manual;
}
