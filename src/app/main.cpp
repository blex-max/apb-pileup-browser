#include <plog/Initializers/RollingFileInitializer.h>
#include <plog/Log.h>

#include <cstdlib>
#include <expected>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "app/manual.hpp"
#include "app/orch.hpp"
#include "app/state.hpp"
#include "argparse/argparse.hpp"
#include "backend/PileupDB.hpp"
#include "backend/schema.hpp"
#include "demo/demo.hpp"
#include "shared/err.hpp"

// Defined in CMakeLists.txt
#ifndef APB_VERSION
#define APB_VERSION "undef"
#endif

// NOTE: helptext is not constructed from
// CLI definition. Must regularly check they
// have not drifted.
static constexpr std::string_view cliHelp =
    R"txt(usage: apb [options] MODE [FILE] [LOCI] [REF]

 apb is an terminal-based genome browser designed for viewing
 and querying pileup loci. It features an easy-to-navigate
 interface and powerful SQL-based query syntax.

 Print the manual with `apb --manual` for extended help.

 Type `help` and press enter in the TUI for in-app help.

modes:
  locus  FILE LOCUS [REF]   view a single locus.
                            FILE   alignment file (sam/bam/cram)
                            LOCUS  genomic locus, e.g. chr1:12345
                            REF    reference fasta (optional)
  db     DB                 load from a dumped db.
                            DB     path to db dump
  demo                      view demo data.

options:
  -h, --help          show this help message and exit.
  -v, --version       print version information and exit.
  --dump PATH         convert pileup to sqlite3 database,
                      dump to disk, and exit. PATH may be
                      - to dump to stdout.
                      (invalid in db mode)
  --manual            Print the apb manual to stdout and exit.
  --schema            Print the apb SQL schema to stdout and exit.
  --log PATH          log debug output to file.

 **IMPORTANT**:
  apb displays all coordinate data in 0-based half-open
  coordinates, matching the internal representation of htslib.
  The sole exception is the locus argument to locus mode,
  which is 1-based to match samtools, and the
  representation of loci in VCF.

 On startup your cursor will be focused at the in-app
 command line at the bottom of the TUI. In the TUI,
 type q and press enter or press Ctrl-C twice to quit.

 See README.md for project background and development
 information.
)txt";


enum class ApbMode : uint8_t { locus, demo, db };
struct ApbCliArgs {
  ApbMode mode;
  std::string dbPath;
  std::string alnPath;
  std::string locus;
  std::string refPath;
  std::string dumpPath;
  std::string logPath;
};

static std::expected<ApbCliArgs, std::string> setup_cli (
    int argc, char** argv
)
{
  argparse::ArgumentParser cli (
      "apb", APB_VERSION, argparse::default_arguments::none
  );
  std::string logPath;

  // NOTE: helptext NOT built from
  // CLI; see helptext above. Confirm
  // they match when making changes
  // NOTE: some args perform an action and immediately exit
  // the program.
  cli.add_argument ("-h", "--help")
      .action ([] (const auto&) {
        std::cout << cliHelp << "\n";
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
  cli.add_argument ("--schema").flag().action ([] (const auto&) {
    std::cout << schema::sqlCreateReadsTable;
    std::exit (EXIT_SUCCESS);
  });

  cli.add_argument ("--dump").metavar ("PATH");
  cli.add_argument ("--log")
      .nargs (1)
      .metavar ("PATH")
      .store_into (logPath);

  cli.add_argument ("MODE").choices ("locus", "db", "demo");
  cli.add_argument ("ARGS").nargs (0, 3).default_value (
      std::vector<std::string>{}
  );

  try {
    cli.parse_args (argc, argv);
  }
  catch (const std::exception& ex) {
    std::ostringstream oss;
    oss << ex.what() << "\n" << cliHelp << "\n";
    return std::unexpected (oss.str());
  }

  ApbCliArgs parsedArgs;
  parsedArgs.logPath = logPath;

  const auto& mode = cli.get<std::string> ("MODE");
  const auto& argPack =
      cli.get<std::vector<std::string>> ("ARGS");
  if (mode == "locus") {
    if (argPack.size() < 2 || argPack.size() > 3) {
      return std::unexpected (
          "locus mode expects FILE LOCUS [REF]"
      );
    }
    parsedArgs.mode = ApbMode::locus,
    parsedArgs.alnPath = argPack[0],
    parsedArgs.locus = argPack[1];
    if (argPack.size() == 3) {
      parsedArgs.refPath = argPack[2];
    }
    if (const auto& dumpPath =
            cli.present<std::string> ("--dump")) {
      parsedArgs.dumpPath = *dumpPath;
    }
  }
  else if (mode == "demo") {
    if (!argPack.empty()) {
      return std::unexpected ("demo mode takes no arguments");
    }
    parsedArgs.mode = ApbMode::demo;
  }
  else if (mode == "db") {
    if (argPack.size() != 1) {
      return std::unexpected ("db mode expects DB");
    }
    if (cli.present<std::string> ("--dump")) {
      return std::unexpected ("--dump is not valid in db mode");
    }
    parsedArgs.mode = ApbMode::db;
    parsedArgs.dbPath = argPack[0];
  }
  else {
    std::unreachable();
  }

  return parsedArgs;
}

namespace welcome {
static constexpr std::string_view msg{
    "Welcome to apb! Type '?' and press Enter for help. All "
    "coordinate data is 0-indexed."
};
}

using VoidOrFailMsg = std::expected<void, std::string>;

static VoidOrFailMsg populate_db_mode_demo (PileupDB& db)
{
  PLOGD << "Inserting demo data into pileup db";
  constexpr hts_pos_t demoGOffset = 10'000'000;
  auto demoRet = insert_demo_data (db, 300, 100, demoGOffset);
  if (!demoRet) {
    return std::unexpected (demoRet.error().msg);
  }

  return {};
}

static VoidOrFailMsg populate_db_mode_db (
    PileupDB& db, std::string_view dbPath
)
{
  PLOGD << "Loading db from disk";
  auto loadRet = load_from_disk (db, dbPath);
  if (!loadRet) {
    return std::unexpected (loadRet.error().msg);
  }

  return {};
}

static VoidOrFailMsg populate_db_mode_locus (
    PileupDB& db, std::string_view alnPath,
    std::string_view locus,
    std::optional<std::string_view> refPath
)
{
  PLOGD << "Opening alignment file";
  auto alnRet = load_aln (std::string{alnPath}.c_str());
  if (!alnRet) {
    return std::unexpected (alnRet.error().msg);
  }
  auto aln = std::move (*alnRet);

  PLOGD << "Parsing locus string";
  PileupPosition pos{};
  hts_pos_t _pend = 1;  // required by htslib, not used here
  if (hts_parse_region (
          std::string{locus}.c_str(), &pos.tid, &pos.pos, &_pend,
          reinterpret_cast<hts_name2id_f> (sam_hdr_name2tid),
          aln.o_hdr, HTS_PARSE_ONE_COORD
      ) == NULL) {
    std::string locusParseErr{"Could not parse locus string "};
    locusParseErr += locus;
    locusParseErr += "; ";
    if (pos.tid < 0) {
      locusParseErr += "invalid contig";
    }
    else {
      locusParseErr += "malformed";
    }
    return std::unexpected (locusParseErr);
  }

  std::optional<FastaFile> ff;
  if (refPath) {
    PLOGD << "Opening reference fasta file";
    auto ffRet = load_fasta (std::string{*refPath}.c_str());
    if (!ffRet) {
      return std::unexpected (ffRet.error().msg);
    }
    ff.emplace (std::move (*ffRet));
  }

  PLOGD << "Inserting pileup";
  auto irRet = insert_pileup (db, aln, pos, ff);
  if (!irRet) {
    return std::unexpected (irRet.error().msg);
  }

  return {};
}

static VoidOrFailMsg run_frontend (
    PileupDB& db, std::string_view welcome_msg
)
{
  // load frontend
  auto stateRet = init (db, welcome_msg);
  if (!stateRet) {
    shutdown();  // would be nice if shutdown was run on state going out of scope...
    return std::unexpected (stateRet.error().msg);
  }
  AppState state = std::move (*stateRet);

  auto loopRet = loop (state);
  if (!loopRet) {
    shutdown();
    return std::unexpected (loopRet.error().msg);
  }

  shutdown();
  return {};
}


int main (int argc, char** argv)
{
  auto argRet = setup_cli (argc, argv);
  if (!argRet) {
    std::cerr << argRet.error() << std::endl;
    return EXIT_FAILURE;
  }
  ApbCliArgs args = *argRet;

  plog::init (
      plog::debug, args.logPath.c_str(),
      1000000 /* 10mb limit */, 1
  );
  PLOGD << "Startup";

  PLOGD << "Creating database";
  PileupDB db;
  auto initRet = init_db (db);
  if (!initRet) {
    std::cerr << "Error: failed to create database. Reporting: "
              << initRet.error().msg << std::endl;
  }

  VoidOrFailMsg popRet;
  switch (args.mode) {
    case ApbMode::locus:
      popRet = populate_db_mode_locus (
          db, args.alnPath, args.locus,
          (args.refPath.empty()) ? std::nullopt
                                 : std::optional (args.refPath)
      );
      break;
    case ApbMode::demo:
      popRet = populate_db_mode_demo (db);
      break;
    case ApbMode::db:
      popRet = populate_db_mode_db (db, args.dbPath);
      break;
  }

  if (!popRet) {
    std::cerr << popRet.error() << std::endl;
    return EXIT_FAILURE;
  }

  if (!args.dumpPath.empty()) {
    PLOGD << "Dumping db";
    auto dumpRet = (args.dumpPath == "-")
                       ? dump_to_stdout (db)
                       : dump_to_disk (db, args.dumpPath);
    if (!dumpRet) {
      std::cerr << "Error: failed to dump database. Reporting: "
                << dumpRet.error().msg << std::endl;
    }
    return EXIT_SUCCESS;
  }

  auto runRet = run_frontend (db, welcome::msg);
  if (!runRet) {
    std::cerr << "Error: failed to load frontend. Reporting: "
              << runRet.error() << std::endl;
  }

  std::cerr << "Bye!" << std::endl;

  return EXIT_SUCCESS;
}
