#include <fmt/format.h>
#include <htslib/sam.h>
#include <plog/Initializers/RollingFileInitializer.h>
#include <plog/Log.h>

#include <cstdlib>
#include <expected>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "app/event.hpp"
#include "app/manual.hpp"
#include "app/state.hpp"
#include "argparse/argparse.hpp"
#include "backend/hts_sql.hpp"
#include "backend/hts_types.hpp"
#include "backend/schema.hpp"
#include "demo/demo.hpp"
#include "shared/apb_assert.hpp"
#include "shared/cleanup.hpp"
#include "shared/version.hpp"

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
)txt"
    "\napb version " APB_VERSION "\n";


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
);

[[nodiscard]] static std::expected<void, std::string>
populate_db_mode_locus (
    PileupDB& db, std::string_view alnPath, std::string_view locus,
    std::optional<std::string_view> refPath
);

// Formats an sqlite3 return code into a user-facing error.
static std::string describe_sqlite_failure (
    int rc, std::string_view context,
    std::optional<std::string_view> dbMsg = std::nullopt
)
{
  switch (rc & 0xFF) {  // strip extended result code
    case SQLITE_CANTOPEN:
      return fmt::format (
          "Failed to {}: could not open the file ({}).", context,
          sqlite3_errstr (rc)
      );
    case SQLITE_PERM:
    case SQLITE_READONLY:
      return fmt::format (
          "Failed to {}: permission denied ({}).", context,
          sqlite3_errstr (rc)
      );
    case SQLITE_NOTADB:
    case SQLITE_CORRUPT:
      return fmt::format (
          "Failed to {}: the file is not a valid sqlite3 "
          "database, or is corrupt ({}).",
          context, sqlite3_errstr (rc)
      );
    case SQLITE_FULL:
    case SQLITE_IOERR:
      return fmt::format (
          "Failed to {}: a disk I/O error occurred ({}).", context,
          sqlite3_errstr (rc)
      );
    default:
      return fmt::format (
          "Failed to {}, reporting code {} and status {} - "
          "please report this failure to the maintainer.",
          context, rc, dbMsg.value_or (sqlite3_errstr (rc))
      );
  }
}

int main (int argc, char** argv)
{
  auto argRet = setup_cli (argc, argv);
  if (!argRet) {
    std::cerr << argRet.error() << std::endl;
    return EXIT_FAILURE;
  }
  ApbCliArgs args = *argRet;

  if (!args.logPath.empty()) {
    if (std::ofstream logTest (args.logPath, std::ios::app); !logTest) {
      std::cerr
          << fmt::format (
                 "Warning: could not open log file at {}; continuing "
                 "without logging",
                 args.logPath
             )
          << std::endl;
      args.logPath.clear();
    }
  }

  plog::init (
      plog::debug, args.logPath.c_str(), 1000000 /* 10mb limit */, 1
  );

  auto db{PileupDB::init()};

  switch (args.mode) {
    case ApbMode::locus:
      if (const auto popRet = populate_db_mode_locus (
              db, args.alnPath, args.locus,
              (args.refPath.empty()) ? std::nullopt
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
      insert_demo_data (db, demoData);
      break;
    }
    case ApbMode::db:
      switch (const auto loadStatus =
                  PileupDB::load_from_disk (db, args.dbPath);
              loadStatus.code) {
        case PileupDB::LoadStatus::openFail:
          std::cerr << fmt::format (
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
          std::cerr << "Error: "
                    << describe_sqlite_failure (
                           loadStatus.sqlRc.value(), "load database",
                           loadStatus.sqlMsg
                       )
                    << std::endl;
          return EXIT_FAILURE;
        case PileupDB::LoadStatus::contentCorrupt:
          std::cerr << fmt::format (
                           "Error: database at {} appears to be "
                           "corrupt:\n{}",
                           args.dbPath, loadStatus.sqlMsg.value()
                       )
                    << std::endl;
          return EXIT_FAILURE;
        case PileupDB::LoadStatus::schemaMismatch:
          std::cerr << fmt::format (
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
          std::cerr << "Error: "
                    << describe_sqlite_failure (
                           dumpStatus.sqlRc.value(),
                           "dump database to disk", dumpStatus.dumpDbMsg
                       )
                    << std::endl;
          return EXIT_FAILURE;
      }
    }
    // dump succeeded, don't launch TUI.
    return EXIT_SUCCESS;
  }

  auto locusInfo = query::get_locus_data (db);
  auto prepResult =
      query::DynamicSelectReadsStmt::prepare_select_reads (db, {});
  if (!prepResult) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to prepare startup query: rc {} ({}): {}",
            prepResult.error(), sqlite3_errstr (prepResult.error()),
            sqlite3_errmsg (db)
        )
    );
  }
  auto startupStmt = std::move (*prepResult);
  // count, and as a consequence verify data presence.
  auto rowCountResult = query::count_rows (startupStmt);
  if (!rowCountResult) {
    APB_UNREACHABLE (
        fmt::format (
            "failed to run startup query: rc {} ({}): {}",
            rowCountResult.error(),
            sqlite3_errstr (rowCountResult.error()), sqlite3_errmsg (db)
        )
    );
  }

  // NOTE: state takes ownership of db; db is now moved-from and
  // must not be referenced again below.
  AppState state{
      .db = {
          .db = std::move (db),
          .stmt = std::move (startupStmt),
          .userClause = {},
          .nStmtRows = *rowCountResult,
          .locusInfo = std::move (locusInfo)
      }
  };
  state.ui.cmd.msgBuf =
      "Welcome to apb! All coordinate data is 0-indexed.";

  if (setlocale (LC_ALL, "") == nullptr) {
    std::cerr << "Warning: could not set locale from environment; "
                 "continuing with the default locale. Non-ASCII "
                 "characters may not display correctly."
              << std::endl;
  }

  // init termbox2!
  //
  // Since this may fail, one might think it
  // should be checked before doing any pileup work. But since
  // it is reasonably unlikely to fail and will affect the
  // terminal as soon as tb_init is called, it's best to leave it here.
  if (const auto rc = tb_init(); rc != TB_OK) {
    // Most of tb_init's other failure codes lose their errno text
    // before we ever see them: tb_init calls tb_shutdown() ->
    // tb_reset() internally on any failure, and tb_reset() zeroes
    // the errno it just recorded. Only TB_ERR_INIT_OPEN survievs.
    // So we have to word manually.
    switch (rc) {
      case TB_ERR_INIT_ALREADY:
      case TB_ERR_MEM:
        APB_UNREACHABLE (
            fmt::format (
                "tb_init failed with code {} ({}) - this should be "
                "impossible",
                rc, tb_strerror (rc)
            )
        );
      case TB_ERR_INIT_OPEN:
        std::cerr << fmt::format (
                         "Error: could not open the terminal ({}). "
                         "Is apb running in an interactive terminal?",
                         tb_strerror (rc)
                     )
                  << std::endl;
        return EXIT_FAILURE;
      case TB_ERR_NO_TERM:
      case TB_ERR_UNSUPPORTED_TERM:
        // TODO what values of TERM does apb support?? Document.
        std::cerr << fmt::format (
                         "Error: {}. Try a different terminal, or "
                         "set TERM to something apb supports (e.g. "
                         "xterm).",
                         tb_strerror (rc)
                     )
                  << std::endl;
        return EXIT_FAILURE;
      default:
        std::cerr << fmt::format (
                         "Error: failed to initialise the terminal "
                         "(code {}). Try again, or in a different "
                         "terminal - if this persists, please "
                         "report it to the maintainer.",
                         rc
                     )
                  << std::endl;
        return EXIT_FAILURE;
    }
  }
  // NOTE: cleanup should be invoked before
  // writing to stderr.
  Defer tb_cleanup ([]() { tb_shutdown(); });
  tb_set_input_mode (TB_INPUT_ALT);
  tb_clear();

  {
    // render first frame
    if (!size_widgets (state.ui)) {
      tb_cleanup.invoke();
      std::cerr << "Terminal too small to display TUI! Try resizing?"
                << std::endl;
      return EXIT_FAILURE;
    }

    switch (const auto dmuStatus =
                draw_main_ui (state.ui, state.db, state.conf);
            dmuStatus.code) {
      case WidgetStatus::success:
        break;
      case WidgetStatus::insufficientSz:
        tb_cleanup.invoke();
        std::cerr << "Terminal too small to display TUI! Try resizing?"
                  << std::endl;
        return EXIT_FAILURE;
    }
    // show command line startup message
    e2::write_string (
        first (state.ui.cmd.inputLine),
        last (state.ui.cmd.inputLine.xspan), "type here - try `help`",
        TB_DIM
    );
    if (const auto rc = tb_present(); rc != TB_OK) {
      if (rc != TB_ERR) {
        APB_UNREACHABLE (
            fmt::format (
                "tb_present failed with code {} ({}) - this should be "
                "impossible",
                rc, tb_strerror (rc)
            )
        );
      }
      const auto msg = fmt::format (
          "lost connection to the terminal ({}). Try running apb "
          "again.",
          tb_strerror (rc)
      );
      tb_cleanup.invoke();
      std::cerr << "Error: " << msg << std::endl;
      return EXIT_FAILURE;
    }
  }

  tb_event ev{};
  while (state.conf.run) {
    tb_poll_event (&ev);
    switch (const auto evStatus = handle_event (state, ev);
            evStatus.code) {
      case WidgetStatus::success:
      case WidgetStatus::insufficientSz:
        // do nothing - allow user to resize terminal
        // rather than crashing.
        break;
    }
    tb_clear();

    switch (const auto dmuStatus =
                draw_main_ui (state.ui, state.db, state.conf);
            dmuStatus.code) {
      case WidgetStatus::success:
        break;
      case WidgetStatus::insufficientSz:
        tb_cleanup.invoke();
        std::cerr << "Terminal too small to display TUI! Try resizing?"
                  << std::endl;
        return EXIT_FAILURE;
    }

    if (state.conf.showOverlay) {
      // For help overlays
      draw_overlay (state.ui.overlay);
    }

    if (const auto rc = tb_present(); rc != TB_OK) {
      if (rc != TB_ERR) {
        APB_UNREACHABLE (
            fmt::format (
                "tb_present failed with code {} ({}) - this should be "
                "impossible",
                rc, tb_strerror (rc)
            )
        );
      }
      const auto msg = fmt::format (
          "lost connection to the terminal ({}). Try running apb "
          "again.",
          tb_strerror (rc)
      );
      tb_cleanup.invoke();
      std::cerr << "Error: " << msg << std::endl;
      return EXIT_FAILURE;
    }

    PLOGD << "Processed frame";
  }
  tb_cleanup.invoke();

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
  cli.add_argument ("--log").nargs (1).metavar ("PATH").store_into (
      logPath
  );

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
  const auto& argPack = cli.get<std::vector<std::string>> ("ARGS");
  if (mode == "locus") {
    if (argPack.size() < 2 || argPack.size() > 3) {
      return std::unexpected ("locus mode expects FILE LOCUS [REF]");
    }
    parsedArgs.mode = ApbMode::locus, parsedArgs.alnPath = argPack[0],
    parsedArgs.locus = argPack[1];
    if (argPack.size() == 3) {
      parsedArgs.refPath = argPack[2];
    }
    if (const auto& dumpPath = cli.present<std::string> ("--dump")) {
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
    if (argPack.empty()) {
      return std::unexpected ("db mode expects DB");
    }
    if (argPack.size() > 1) {
      return std::unexpected ("db mode expects only a single DB argument");
    }
    if (cli.present<std::string> ("--dump")) {
      return std::unexpected ("--dump is not valid in db mode");
    }
    parsedArgs.mode = ApbMode::db;
    parsedArgs.dbPath = argPack[0];
  }
  else {
    APB_UNREACHABLE ("unrecognised mode");
  }

  return parsedArgs;
}

// separated into fn for readability
static std::expected<void, std::string> populate_db_mode_locus (
    PileupDB& db, std::string_view alnPath, std::string_view locus,
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
  hts_pos_t pend;
  if (hts_parse_region (
          std::string{locus}.c_str(), &tid, &pos, &pend,
          reinterpret_cast<hts_name2id_f> (sam_hdr_name2tid), aln.o_hdr,
          HTS_PARSE_ONE_COORD
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
  // HTS_PARSE_ONE_COORD accepts range shorthand such as
  // "chr:-100" (== "chr:1-100") or a bare "chr" (== whole
  // contig); reject anything that doesn't resolve to a single
  // coordinate.
  if (pend - pos != 1) {
    return std::unexpected (
        fmt::format (
            "Locus string {} does not specify a single "
            "coordinate; provide one position, e.g. 21:12345",
            locus
        )
    );
  }

  std::string contigName;
  {
    const char* contigNameCStr = sam_hdr_tid2name (aln.o_hdr, tid);
    if (contigNameCStr == NULL) {
      // This should be impossible since we've already done hts_parse_region
      APB_UNREACHABLE (
          fmt::format (
              "Contig with tid {} could not be converted into a "
              "contig name from locus string {}",
              tid, locus
          )
      );
    }
    contigName = contigNameCStr;
  }

  std::optional<FastaFile> ff;
  if (refPath) {
    PLOGD << "Opening reference fasta file";
    auto ffResult = FastaFile::load_fasta (std::string{*refPath}.c_str());
    if (!ffResult) {
      return std::unexpected (
          fmt::format ("Failed to open reference fasta at {}", *refPath)
      );
    }
    ff = std::move (*ffResult);
  }

  PLOGD << "Inserting pileup";
  auto prepareResult = PileupIterator::prepare_pileup_iter (aln, tid, pos);
  if (!prepareResult) {
    switch (prepareResult.error()) {
      case PileupIterator::samItrFail:
        return std::unexpected ("Failed to create alignment iterator");
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

  auto irRet =
      hts2sql::insert_pileup (db, pileupIter, contigName, aln.o_hdr, ff);
  if (!irRet) {
    const auto err = irRet.error();
    switch (err.code) {
      case hts2sql::InsertPileupErr::sqlFail:
        return std::unexpected (describe_sqlite_failure (
            err.sqlRc.value(), "transform/insert alignment data",
            sqlite3_errmsg (db)
        ));
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
                contigName, pileupIter.span.start, pileupIter.span.end
            )
        );
    }
  }

  return {};
}
