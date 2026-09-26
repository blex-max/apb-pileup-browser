#include <fmt/format.h>
#include <htslib/sam.h>
#include <plog/Initializers/RollingFileInitializer.h>
#include <plog/Log.h>

#include <cstdlib>
#include <expected>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/event.hpp"
#include "app/manual.hpp"
#include "app/state.hpp"
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
    R"txt(usage: apb [options] ALN LOCUS [REF]
       apb [options] --demo
       apb [options] --db DB

 apb is an terminal-based genome browser designed for viewing
 and querying pileup loci. It features an easy-to-navigate
 interface and powerful SQL-based query syntax.

 Print the manual with `apb --manual` for extended help.

 Type `help` and press enter in the TUI for in-app help.

arguments:
  ALN     alignment file (sam/bam/cram).
  LOCUS   genomic locus, e.g. chr1:12345 (1-based)
  REF     reference fasta (optional)

options:
  -h, --help          show this help message and exit.
  -v, --version       print version information and exit.
  --demo              view demo data, in place of
                      FILE/LOCUS/REF.
  --db DB             load from a dumped db, in place of
                      FILE/LOCUS/REF. DB path to db dump.
                      (mutually exclusive with --demo)
  --dump PATH         convert pileup to sqlite3 database,
                      dump to disk, and exit.
                      (invalid with --db)
  --manual            Print the apb manual to stdout and exit.
  --schema            Print the apb SQL schema to stdout and exit.
  --log PATH          log debug output to file.
  -0, --zero-based    treat LOCUS as 0-based (e.g. from a BED
                      file) instead of 1-based. Not valid
                      with --demo/--db.

 **IMPORTANT**:
  apb displays all coordinate data, including LOCUS, in
  1-based closed coordinates.

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
  bool zeroBased = false;
};

static std::expected<ApbCliArgs, std::string> setup_cli (
    int argc, char** argv
);

[[nodiscard]] static std::expected<void, std::string>
populate_db_mode_locus (
    PileupDB& db, std::string_view alnPath, std::string_view locus,
    std::optional<std::string_view> refPath, bool zeroBased
);

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
      plog::debug, args.logPath.c_str(), 10000000 /* 10mb limit */, 1
  );

  auto db{PileupDB::init()};

  switch (args.mode) {
    case ApbMode::locus:
      if (const auto popRet = populate_db_mode_locus (
              db, args.alnPath, args.locus,
              (args.refPath.empty()) ? std::nullopt
                                     : std::optional (args.refPath),
              args.zeroBased
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
                    << query::describe_sqlite_failure (
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
    switch (const auto dumpStatus =
                query::dump_to_disk (db, args.dumpPath);
            dumpStatus.code) {
      case query::DiskDumpStatus::success:
        break;
      case query::DiskDumpStatus::fail:
        std::cerr << "Error: "
                  << query::describe_sqlite_failure (
                         dumpStatus.sqlRc.value(), "dump database to disk",
                         dumpStatus.dumpDbMsg
                     )
                  << std::endl;
        return EXIT_FAILURE;
    }
    // dump succeeded, don't launch TUI.
    return EXIT_SUCCESS;
  }

  auto locusInfo = query::get_locus_data (db);
  auto prepResult = query::prepare_select_reads (
      db, schema::ReadTableSelect::sqlPrefix, {}
  );
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
          .userClause = {},
          .selectStmt = std::move (startupStmt),
          .nStmtRows = *rowCountResult,
          .locusInfo = std::move (locusInfo)
      }
  };
  state.ui.cmd.msgBuf =
      "Welcome to apb! All coordinate data is in 1-based closed "
      "coordinates.";

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
  // NOTE: helptext NOT built from
  // CLI; see helptext above. Confirm
  // they match when making changes.

  std::string dumpPath;
  std::string dbPath;
  std::string logPath;
  bool zeroBased = false;
  bool demoRequested = false;
  bool dbRequested = false;
  bool dumpRequested = false;
  std::vector<std::string> argPack;

  auto parseFail = [] (std::string msg) -> std::unexpected<std::string> {
    return std::unexpected (fmt::format ("{}\n{}\n", msg, cliHelp));
  };

  auto takeValue = [&] (
                       int& i, std::string_view flag
                   ) -> std::expected<std::string, std::string> {
    if (i + 1 >= argc) {
      return std::unexpected (
          fmt::format ("{}: expected one argument", flag)
      );
    }
    return std::string (argv[++i]);
  };

  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];

    if (arg == "-h" || arg == "--help") {
      std::cout << cliHelp << "\n";
      std::exit (0);
    }
    else if (arg == "-v" || arg == "--version") {
      std::cout << APB_VERSION << "\n";
      std::exit (0);
    }
    else if (arg == "--manual") {
      std::cout << get_manual();
      std::exit (EXIT_SUCCESS);
    }
    else if (arg == "--schema") {
      std::cout << schema::sqlCreateReadsTable;
      std::exit (EXIT_SUCCESS);
    }
    else if (arg == "-0" || arg == "--zero-based") {
      zeroBased = true;
    }
    else if (arg == "--demo") {
      demoRequested = true;
    }
    else if (arg == "--dump") {
      auto val = takeValue (i, "--dump");
      if (!val) {
        return parseFail (val.error());
      }
      dumpPath = *val;
      dumpRequested = true;
    }
    else if (arg == "--log") {
      auto val = takeValue (i, "--log");
      if (!val) {
        return parseFail (val.error());
      }
      logPath = *val;
    }
    else if (arg == "--db") {
      auto val = takeValue (i, "--db");
      if (!val) {
        return parseFail (val.error());
      }
      dbPath = *val;
      dbRequested = true;
    }
    else if (arg.starts_with ("-")) {
      return parseFail (fmt::format ("unrecognized argument: {}", arg));
    }
    else {
      argPack.emplace_back (arg);
    }
  }

  if (demoRequested && dbRequested) {
    return parseFail ("arguments --db and --demo are mutually exclusive");
  }

  ApbCliArgs parsedArgs;
  parsedArgs.logPath = logPath;

  if (demoRequested) {
    if (!argPack.empty()) {
      return std::unexpected ("--demo takes no positional arguments");
    }
    if (zeroBased) {
      return std::unexpected ("-0/--zero-based is not valid with --demo");
    }
    parsedArgs.mode = ApbMode::demo;
    if (dumpRequested) {
      parsedArgs.dumpPath = dumpPath;
    }
  }
  else if (dbRequested) {
    if (!argPack.empty()) {
      return std::unexpected ("--db takes no positional arguments");
    }
    if (dumpRequested) {
      return std::unexpected ("--dump is not valid with --db");
    }
    if (zeroBased) {
      return std::unexpected ("-0/--zero-based is not valid with --db");
    }
    parsedArgs.mode = ApbMode::db;
    parsedArgs.dbPath = dbPath;
  }
  else {
    if (argPack.size() < 2 || argPack.size() > 3) {
      return std::unexpected (
          "expected ALN LOCUS [REF] (or pass --demo / --db PATH)"
      );
    }
    parsedArgs.mode = ApbMode::locus;
    parsedArgs.alnPath = argPack[0];
    parsedArgs.locus = argPack[1];
    if (argPack.size() == 3) {
      parsedArgs.refPath = argPack[2];
    }
    if (dumpRequested) {
      parsedArgs.dumpPath = dumpPath;
    }
    parsedArgs.zeroBased = zeroBased;
  }

  return parsedArgs;
}

// separated into fn for readability
static std::expected<void, std::string> populate_db_mode_locus (
    PileupDB& db, std::string_view alnPath, std::string_view locus,
    std::optional<std::string_view> refPath, bool zeroBased
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
      default:
        APB_UNREACHABLE ("unrecognised AlnFile load error");
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
  if (zeroBased) {
    // hts_parse_region always treats the input as 1-based and
    // subtracts 1; add it back to recover a caller-supplied 0-based
    // position (e.g. a BED file's start column). pend is unused past
    // this point, so it doesn't need the same adjustment.
    pos += 1;
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
      default:
        APB_UNREACHABLE ("unrecognised PileupIterator prepare error");
    }
  }
  auto pileupIter{std::move (*prepareResult)};

  auto irRet =
      hts2sql::insert_pileup (db, pileupIter, contigName, aln.o_hdr, ff);
  if (!irRet) {
    const auto err = irRet.error();
    switch (err.code) {
      case hts2sql::InsertPileupErr::sqlFail:
        return std::unexpected (
            query::describe_sqlite_failure (
                err.sqlRc.value(), "transform/insert alignment data",
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
                contigName, pileupIter.span.start + 1, pileupIter.span.end
            )
        );
      default:
        APB_UNREACHABLE ("unrecognised InsertPileupErr code");
    }
  }

  return {};
}
