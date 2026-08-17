#include "cli.hpp"

#include <cstdlib>
#include <expected>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "app/text_blocks.hpp"
#include "argparse/argparse.hpp"

// Defined in CMakeLists.txt
#ifndef APB_VERSION
#define APB_VERSION "undef"
#endif

static constexpr std::string_view CLI_HELP =
    R"txt(usage: apb [options] MODE [FILE] [LOCI] [REF]

 apb is an terminal-based genome browser designed for viewing
 and querying pileup loci. It features a REPL-like command
 line and simple SQL-based query syntax.

modes:
  locus  FILE LOCUS [REF]   view a single locus
                            FILE   alignment file (sam/bam/cram)
                            LOCUS  genomic locus, e.g. chr1:12345
                            REF    reference fasta (optional)
  vcf    FILE VCF [REF]     view variant loci from a VCF
                            FILE   alignment file (sam/bam/cram)
                            VCF    VCF file to load loci from
                            REF    reference fasta (optional)
  db     DB                 load from a dumped db
                            DB     path to db dump
  demo                      view demo data

options:
  -h, --help          show this help message and exit
  -v, --version       print version information and exit
  --dump PATH         convert pileup to sqlite3 database, dump to disk, and exit
  --dump-readme PATH  write README.md to PATH and exit
  --log PATH          log debug output to file

 See README.md for further info, or use the in-app help
 (type ? and press enter in the TUI). If you don't have
 the readme, it can be written to disk from the CLI
 using `apb --dump-readme PATH` or from within the TUI
 using `readme PATH`.

 In the TUI, type q and press enter or press Ctrl-C
 twice to quit.)txt";

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

  // NOTE: help, version, dump-readme
  // all exit program
  cli.add_argument ("-h", "--help")
      .action ([] (const auto&) {
        std::cout << CLI_HELP << "\n";
        std::exit (0);
      })
      .default_value (false)
      .implicit_value (true)
      .nargs (0);
  cli.add_argument ("-v", "--version")
      .action ([] (const auto&) {
        std::cout << APB_VERSION << "\n";
        std::exit (0);
      })
      .default_value (false)
      .implicit_value (true)
      .nargs (0);
  cli.add_argument ("--dump-readme")
      .help ("write README.md to PATH and exit")
      .metavar ("PATH")
      .action ([] (const std::string& path) {
        std::ofstream ofs (path);
        if (!ofs) {
          std::cerr << "could not open " << path
                    << " for writing\n";
          std::exit (EXIT_FAILURE);
        }
        ofs << get_readme();
        std::exit (EXIT_SUCCESS);
      });

  cli.add_argument ("--dump")
      .help (
          "convert pileup to sqlite3 database, "
          "dump to disk, and exit."
      )  // headless mode
      .metavar ("PATH");
  cli.add_argument ("--log")
      .help ("log debug output to file")
      .nargs (1)
      .metavar ("PATH")
      .store_into (logPath);

  cli.add_argument ("MODE")
      .help ("locus|vcf|db|demo")
      .choices ("locus", "vcf", "db", "demo");
  cli.add_argument ("ARGS")
      .help ("mode-specific positional arguments; see -h")
      .nargs (0, 3)
      .default_value (std::vector<std::string>{});

  try {
    cli.parse_args (argc, argv);
  }
  catch (const std::exception& ex) {
    std::ostringstream oss;
    oss << ex.what() << "\n" << CLI_HELP << "\n";
    return std::unexpected (make_cli_err (oss.str()));
  }

  auto modeArgsRet = assemble_mode_args (
      cli.get<std::string> ("MODE"),
      cli.get<std::vector<std::string>> ("ARGS"),
      cli.present<std::string> ("--dump")
  );
  if (!modeArgsRet) {
    std::ostringstream oss;
    oss << modeArgsRet.error().msg << "\n" << CLI_HELP << "\n";
    return std::unexpected (make_cli_err (oss.str()));
  }

  return StartupArgs{std::move (*modeArgsRet), logPath};
}
