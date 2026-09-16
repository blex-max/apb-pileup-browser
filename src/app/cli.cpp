#include "cli.hpp"

#include <cstdlib>
#include <expected>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "app/manual.hpp"
#include "argparse/argparse.hpp"

// Defined in CMakeLists.txt
#ifndef APB_VERSION
#define APB_VERSION "undef"
#endif

// NOTE: helptext is not constructed from
// CLI definition. Must regularly check they
// have not drifted.
// TODO: write a prerelease checklist .txt/.md

// NOTE: text for when VCF mode is live.
// vcf    FILE VCF [REF]     view variant loci from a VCF
//                           FILE   alignment file (sam/bam/cram)
//                           VCF    VCF file to load loci from
//                           REF    reference fasta (optional)

static constexpr std::string_view sh_cliHelp =
    R"txt(usage: apb [options] MODE [FILE] [LOCI] [REF]

 apb is an terminal-based genome browser designed for viewing
 and querying pileup loci. It features an easy-to-navigate
 interface and powerful SQL-based query syntax.

 Print the manual with `apb --manual` for extended help.

 Type ? and press enter in the TUI for in-app help.

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
  --dump PATH         convert pileup to sqlite3 database,
                      dump to disk, and exit
                      (invalid in db mode)
  --manual            Print the apb manual to stdout and exit
  --log PATH          log debug output to file

 **IMPORTANT**:
  apb displays all coordinate data in 0-based half-open
  coordinates, matching the internal representation of htslib.
  The sole exception is the locus argument to locus mode,
  which is 1-based to match samtools, and the
  representation of loci in VCF.

 See README.md for project background and development
 information.

 In the TUI, type q and press enter or press Ctrl-C
 twice to quit.
)txt";

// MODE + variadic positional args.
static std::expected<ModalArgs, Err> assemble_mode_args (
    const std::string& mode,
    const std::vector<std::string>& rest,
    const std::optional<std::string>& dumpPath
)
{
  if (mode == "locus") {
    if (rest.size() < 2 || rest.size() > 3) {
      return std::unexpected (
          make_cli_err ("locus mode expects FILE LOCUS [REF]")
      );
    }
    return AlnModeArgs{
        .alnPath = rest[0],
        .locus = rest[1],
        .refPath = rest.size() == 3 ? std::optional{rest[2]}
                                    : std::nullopt,
        .dumpPath = dumpPath
    };
  }
  if (mode == "vcf") {
    if (rest.size() < 2 || rest.size() > 3) {
      return std::unexpected (
          make_cli_err ("vcf mode expects FILE VCF [REF]")
      );
    }
    return VcfModeArgs{
        .alnPath = rest[0],
        .vcfPath = rest[1],
        .refPath = rest.size() == 3 ? std::optional{rest[2]}
                                    : std::nullopt,
        .dumpPath = dumpPath
    };
  }
  if (mode == "db") {
    if (rest.size() != 1) {
      return std::unexpected (
          make_cli_err ("db mode expects DB")
      );
    }
    if (dumpPath) {
      return std::unexpected (
          make_cli_err ("--dump is not valid in db mode")
      );
    }
    return DbModeArgs{.dbPath = rest[0]};
  }
  // mode == "demo", the only choice left after argparse's .choices() check
  if (!rest.empty()) {
    return std::unexpected (
        make_cli_err ("demo mode takes no arguments")
    );
  }
  return DemoModeArgs{.dumpPath = dumpPath};
}

ArgsOrErr parse_args (int argc, char** argv)
{
  argparse::ArgumentParser cli (
      "apb", APB_VERSION, argparse::default_arguments::none
  );
  std::string logPath;

  // NOTE: helptext NOT built from
  // CLI; see helptext above. Confirm
  // they match when making changes
  // NOTE: help, version, manual
  // all exit program
  cli.add_argument ("-h", "--help")
      .action ([] (const auto&) {
        std::cout << sh_cliHelp << "\n";
        std::exit (0);
      })
      .flag();
  cli.add_argument ("-v", "--version")
      .action ([] (const auto&) {
        std::cout << APB_VERSION << "\n";
        std::exit (0);
      })
      .flag();
  cli.add_argument ("--manual").flag().action ([] (const auto&) {
    std::cout << get_manual();
    std::exit (EXIT_SUCCESS);
  });

  cli.add_argument ("--dump").metavar ("PATH");
  cli.add_argument ("--log")
      .nargs (1)
      .metavar ("PATH")
      .store_into (logPath);

  cli.add_argument ("MODE").choices (
      "locus", "vcf", "db", "demo"
  );
  cli.add_argument ("ARGS").nargs (0, 3).default_value (
      std::vector<std::string>{}
  );

  try {
    cli.parse_args (argc, argv);
  }
  catch (const std::exception& ex) {
    std::ostringstream oss;
    oss << ex.what() << "\n" << sh_cliHelp << "\n";
    return std::unexpected (make_cli_err (oss.str()));
  }

  auto modeArgsRet = assemble_mode_args (
      cli.get<std::string> ("MODE"),
      cli.get<std::vector<std::string>> ("ARGS"),
      cli.present<std::string> ("--dump")
  );
  if (!modeArgsRet) {
    std::ostringstream oss;
    oss << modeArgsRet.error().msg << "\n" << sh_cliHelp << "\n";
    return std::unexpected (make_cli_err (oss.str()));
  }

  return StartupArgs{std::move (*modeArgsRet), logPath};
}
