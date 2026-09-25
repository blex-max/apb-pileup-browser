#include <fmt/format.h>
#include <htslib/sam.h>
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
#include "backend/hts_sql.hpp"
#include "backend/hts_types.hpp"
#include "backend/schema.hpp"
#include "demo/demo.hpp"

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

namespace welcome {
static constexpr std::string_view msg{
    "Welcome to apb! All coordinate data is 0-indexed."
};
}

static std::expected<ApbCliArgs, std::string> setup_cli (
    int argc, char** argv
);

[[nodiscard]] static std::expected<void, std::string>
populate_db_mode_locus (
    PileupDB& db, std::string_view alnPath,
    std::string_view locus,
    std::optional<std::string_view> refPath
);

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

  auto initResult = PileupDB::init();
  if (!initResult) {
    std::cerr
        << fmt::format (
               "Error: sqlite3 operation failed during "
               "initalisation of database, reporting code {} "
               "- please report "
               "this failure to the maintainer",
               initResult.error()
           )
        << std::endl;
    return EXIT_FAILURE;
  }
  auto db{std::move (*initResult)};

  switch (args.mode) {
    case ApbMode::locus:
      if (const auto popRet = populate_db_mode_locus (
              db, args.alnPath, args.locus,
              (args.refPath.empty())
                  ? std::nullopt
                  : std::optional (args.refPath)
          );
          !popRet) {
        std::cerr << "Error: " << popRet.error() << std::endl;
        return EXIT_FAILURE;
      }
      break;
    case ApbMode::demo: {
      constexpr hts_pos_t demoGOffset = 10'000'000;
      DemoDataPack demoData;
      generate_demo_data (300, 100, demoGOffset, demoData);
      if (const auto rc = insert_demo_data (db, demoData)) {
        std::cerr
            << fmt::format (
                   "Failed to transform/insert data to internal "
                   "database, reporting code {} "
                   "and status {} - please report this failure "
                   "to the "
                   "maintainer",
                   rc, sqlite3_errmsg (db)
               )
            << std::endl;
        return EXIT_FAILURE;
      }
      break;
    }
    case ApbMode::db:
      switch (const auto loadStatus =
                  PileupDB::load_from_disk (db, args.dbPath);
              loadStatus.code) {
        case PileupDB::LoadStatus::openFail:
          std::cerr
              << fmt::format (
                     "Error: failed to open database at {}, "
                     "reporting error: {}; and extended status "
                     "msg: "
                     "{}",
                     args.dbPath,
                     sqlite3_errstr (loadStatus.sqlRc.value()),
                     loadStatus.sqlMsg.value()
                 )
              << std::endl;
          return EXIT_FAILURE;
        case PileupDB::LoadStatus::copyFail:
          std::cerr
              << fmt::format (
                     "Error: sqlite3 operation failed during "
                     "loading of database, reporting "
                     "code {} "
                     "and status {} - please report "
                     "this failure to the maintainer",
                     loadStatus.sqlRc.value(),
                     loadStatus.sqlMsg.value()
                 )
              << std::endl;
          return EXIT_FAILURE;
        case PileupDB::LoadStatus::verifyError:
          std::cerr << fmt::format (
                           "Error: sqlite3 operation failed during "
                           "verification of database, reporting "
                           "code {} - please report "
                           "this failure to the maintainer",
                           loadStatus.sqlRc.value()
                       )
                    << std::endl;
          return EXIT_FAILURE;
        case PileupDB::LoadStatus::schemaMismatch:
          std::cerr
              << fmt::format (
                     "Error: database at {} does not have the "
                     "expected schema for an apb database. Is "
                     "it "
                     "from an old version?",
                     args.dbPath
                 )
              << std::endl;
          return EXIT_FAILURE;
        case PileupDB::LoadStatus::success:
          break;
      }
      break;
  }


  if (!args.dumpPath.empty()) {
    if (args.dumpPath == "-") {
      switch (const auto dumpStatus = query::dump_to_stdout (db);
              dumpStatus.code) {
        case query::StdoutDumpStatus::success:
          break;
        case query::StdoutDumpStatus::sqliteSerialiseFail:
          std::cerr << fmt::format (
                           "Error: failed to serialise "
                           "database. Please "
                           "report this to the maintainer."
                       )
                    << std::endl;
          return EXIT_FAILURE;
        case query::StdoutDumpStatus::writeFail:
          std::cerr << "Error: failed to write database to "
                       "stdout. Output may be corrupted."
                    << std::endl;
          return EXIT_FAILURE;
      }
    }
    else {
      switch (const auto dumpStatus =
                  query::dump_to_disk (db, args.dumpPath);
              dumpStatus.code) {
        case query::DiskDumpStatus::success:
          break;
        case query::DiskDumpStatus::fail:
          std::cerr << fmt::format (
                           "Error: failed to dump database to "
                           "disk, reporting code {} and status "
                           "{} - please report to maintainer.",
                           dumpStatus.sqlRc.value(),
                           dumpStatus.dumpDbMsg.value()
                       )
                    << std::endl;
          return EXIT_FAILURE;
      }
    }
    // dump and exit
    return EXIT_SUCCESS;
  }

  auto stateRet = init_tui_state (db, welcome::msg);
  if (!stateRet) {
    std::cerr << fmt::format (
                     "Error: sqlite3 operation failed during "
                     "initalisation of TUI, reporting code {} "
                     "- {} and status {} - please report "
                     "this failure to the maintainer",
                     stateRet.error(),
                     sqlite3_errstr (stateRet.error()),
                     sqlite3_errmsg (db)
                 )
              << std::endl;
    return EXIT_FAILURE;
  }
  // NOTE: state object has taken ownership of db.
  // db object is now nulled.
  AppState state = std::move (*stateRet);

  switch (const auto loopExitStatus = run_tui_loop (state);
          loopExitStatus.code) {
    case TuiStatus::success:
      break;
    case TuiStatus::insufficientSz:
      std::cerr
          << "Terminal too small to display TUI! Try resizing?"
          << std::endl;
      return EXIT_FAILURE;
    case TuiStatus::sqlFail:
      std::cerr << fmt::format (
                       "Error: sqlite3 operation failed during "
                       "TUI main loop, reporting code {} "
                       "- {} and status {} - please report "
                       "this failure to the maintainer",
                       loopExitStatus.sqlRc.value(),
                       sqlite3_errstr (loopExitStatus.sqlRc.value()),
                       sqlite3_errmsg (state.db.db)
                   )
                << std::endl;
      return EXIT_FAILURE;
  }

  std::cerr << "Bye!" << std::endl;

  return EXIT_SUCCESS;
}


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
    if (const auto& dumpPath = cli.present<std::string> ("--dump")) {
      parsedArgs.dumpPath = *dumpPath;
    }
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


static std::expected<void, std::string> populate_db_mode_locus (
    PileupDB& db, std::string_view alnPath,
    std::string_view locus,
    std::optional<std::string_view> refPath
)
{
  PLOGD << "Opening alignment file";
  auto alnRet = AlnFile::load_aln (std::string{alnPath});
  if (!alnRet) {
    switch (alnRet.error()) {
      case AlnFile::openFail:
        return std::unexpected ("Failed to open alignment file");
      case AlnFile::hdrReadFail:
        return std::unexpected (
            "Failed to read alignment file header; is it "
            "corrupt?"
        );
      case AlnFile::indexLoadFail:
        return std::unexpected (
            "Failed to load index file for alignment; is the "
            "file indexed?"
        );
    }
  }
  auto aln = std::move (*alnRet);

  PLOGD << "Parsing locus string";
  int32_t tid;
  hts_pos_t pos;
  hts_pos_t _pend = 1;  // required by htslib, not used here
  if (hts_parse_region (
          std::string{locus}.c_str(), &tid, &pos, &_pend,
          reinterpret_cast<hts_name2id_f> (sam_hdr_name2tid),
          aln.o_hdr, HTS_PARSE_ONE_COORD
      ) == NULL) {
    std::string locusParseErr{"Could not parse locus string "};
    locusParseErr += locus;
    locusParseErr += "; ";
    if (tid < 0) {
      locusParseErr += "invalid contig";
    }
    else {
      locusParseErr += "malformed";
    }
    return std::unexpected (locusParseErr);
  }

  std::string contigName;
  {
    const char* contigNameCStr =
        sam_hdr_tid2name (aln.o_hdr, tid);
    if (contigNameCStr == NULL) {
      // This should be impossible since we've already done hts_parse_region
      return std::unexpected (
          fmt::format (
              "Contig with tid {} could not be converted into a "
              "contig name from locus string {} - this should "
              "not occur, please report to the maintainer.",
              tid, locus
          )
      );
    }
    contigName = contigNameCStr;
  }

  std::optional<FastaFile> ff;
  if (refPath) {
    PLOGD << "Opening reference fasta file";
    auto ffResult =
        FastaFile::load_fasta (std::string{*refPath}.c_str());
    if (!ffResult) {
      return std::unexpected (
          fmt::format (
              "Failed to open reference fasta at {}", *refPath
          )
      );
    }
    ff = std::move (*ffResult);
  }

  PLOGD << "Inserting pileup";
  auto prepareResult =
      PileupIterator::prepare_pileup_iter (aln, tid, pos);
  if (!prepareResult) {
    switch (prepareResult.error()) {
      case PileupIterator::samItrFail:
        return std::unexpected (
            "Failed to create alignment iterator"
        );
      case PileupIterator::pileupInitFail:
        return std::unexpected (
            "Failed to initialise htslib pileup iterator"
        );
      case PileupIterator::pileupIterateFail:
        return std::unexpected ("Failed to iterate pileup");
      case PileupIterator::locusNotCovered:
        return std::unexpected (
            "No reads in alignment file align to this locus"
        );
    }
  }
  auto pileupIter{std::move (*prepareResult)};

  auto irRet = hts2sql::insert_pileup (
      db, pileupIter, contigName, aln.o_hdr, ff
  );
  if (!irRet) {
    const auto err = irRet.error();
    switch (err.code) {
      case hts2sql::InsertPileupErr::sqlFail:
        return std::unexpected (
            fmt::format (
                "Failed to transform/insert data to internal "
                "database, reporting code {} - {} "
                "and status {} - please report this failure to "
                "the "
                "maintainer",
                err.sqlRc.value(),
                sqlite3_errstr (err.sqlRc.value()),
                sqlite3_errmsg (db)
            )
        );
      case hts2sql::InsertPileupErr::auxParseFail:
        // TODO: provide qname/read/tag details
        return std::unexpected (
            "Failed to parse aux tag in alignment file. Is aux "
            "data corrupt?"
        );
      case hts2sql::InsertPileupErr::refFetchFail:
        return std::unexpected (
            fmt::format (
                "Failed to fetch reference region from fasta "
                "for "
                "span {}:{}-{}",
                contigName, pileupIter.span.start,
                pileupIter.span.end
            )
        );
    }
  }

  return {};
}
