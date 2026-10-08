#include <fmt/format.h>
#include <fmt/std.h>
#include <htslib/faidx.h>
#include <htslib/hts.h>
#include <htslib/sam.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <iostream>
#include <map>
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
#include "shared/bounds.hpp"
#include "shared/cleanup.hpp"
#include "shared/version.hpp"

// NOTE: helptext is not constructed from
// CLI definition. Must regularly check they
// have not drifted.
static constexpr std::string_view cliHelp =
    R"txt(usage: apb [options] LOCUS ALN...

 apb is an terminal-based genome browser designed for
 viewing and querying reads at specific loci. It features
 an easy-to-navigate interface and powerful SQL-based query
 syntax.

 Print the manual with `apb --manual` for a complete, concise
 guide to using apb. It is best read with a pager, e.g.
 `apb --manual | less`.

 Type `help` and press enter in the TUI for in-app help.

arguments:
  ALN...    alignment file/s (sam/bam/cram).
  LOCUS     genomic locus, e.g. chr1:12345 (1-based)

options:
  -r, --ref FASTA     reference fasta; TUI will display divergence
                      (invalid with --demo/--db)
  --demo              view demo data, in place of
                      LOCUS/ALN.
  --db DB             load from a dumped db, in place of
                      LOCUS/ALN. DB path to db dump.
                      (mutually exclusive with --demo)
  --dump PATH         convert pileup to sqlite3 database,
                      dump to disk, and exit.
                      (invalid with --db)
  --manual            Print the apb manual to stdout and exit.
  --log               print extra startup/shutdown diagnostics to
                      stderr.
  -0, --zero-based    treat LOCUS as 0-based (e.g. from a BED
                      file) instead of 1-based. Not valid
                      with --demo/--db.
  -h, --help          show this help message and exit.
  -v, --version       print version information and exit.

 **IMPORTANT**:
  apb displays all coordinate data in
  1-based closed coordinates.

 On startup your cursor will be focused at the in-app
 command line at the bottom of the TUI. In the TUI,
 type q and press enter or press Ctrl-C twice to quit.

 See README.md for project background and development
 information.
)txt"
    "\napb version " APB_VERSION "\n";


enum class ApbMode : uint8_t { locus, demo, db, UNSET };
struct ApbCliArgs {
  ApbMode mode = ApbMode::UNSET;
  std::string locus;
  // label: shortest trailing path suffix distinct among all alns
  struct AlnInput {
    std::filesystem::path path;
    std::string label;
  };
  std::vector<AlnInput> alns;

  std::optional<std::filesystem::path> refPath = std::nullopt;
  std::optional<std::filesystem::path> dumpPath = std::nullopt;
  std::optional<std::filesystem::path> dbPath = std::nullopt;

  bool verboseLog = false;
  bool zeroBased = false;
};

static std::expected<ApbCliArgs, std::string> setup_cli (
    int argc, char** argv
);

[[nodiscard]] static std::expected<void, std::string>
populate_db_mode_locus (
    PileupDB& db, const std::vector<ApbCliArgs::AlnInput>& alns,
    const std::string& locus,
    const std::optional<std::filesystem::path>& refPath, bool zeroBased
);

int main (int argc, char** argv)
{
  /* setup */
  auto argRet = setup_cli (argc, argv);
  if (!argRet) {
    std::cerr << argRet.error() << std::endl;
    return EXIT_FAILURE;
  }
  ApbCliArgs args = *argRet;
  const bool verboseLog = args.verboseLog;

  /* config */
  // must configure before sqlite3 is initialised (which will happen
  // when database is initalised).
  if (const auto rc = sqlite3_config (SQLITE_CONFIG_MEMSTATUS, 1);
      verboseLog && rc != SQLITE_OK) {
    std::cerr << "Could not enable sqlite3 memory reporting. Memory "
                 "stats will be invalid"
              << std::endl;
  }
#ifdef HAVE_HEAP_LIM
  if (const auto rc = sqlite3_hard_heap_limit64 (kMaxSqliteHeapBytes);
      verboseLog && rc != SQLITE_OK) {
    std::cerr << "Hard heap limit failed, database size uncapped"
              << std::endl;
  }
#else
  if (verboseLog) {
    std::cerr << "Hard heap limiting not available, database size uncapped"
              << std::endl;
  }
#endif

  auto db{PileupDB::init()};

  switch (args.mode) {
    case ApbMode::locus:
      if (const auto popRet = populate_db_mode_locus (
              db, args.alns, args.locus, args.refPath, args.zeroBased
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
      switch (const auto loadStatus = PileupDB::load_from_disk (
                  db, args.dbPath.value().string()
              );
              loadStatus.code) {
        case PileupDB::LoadStatus::openFail:
          std::cerr << fmt::format (
                           "Error: failed to open database at {}, "
                           "reporting error: {}; and extended status "
                           "msg: "
                           "{}",
                           args.dbPath.value(),
                           sqlite3_errstr (loadStatus.rc.value()),
                           loadStatus.msg.value()
                       )
                    << std::endl;
          return EXIT_FAILURE;
        case PileupDB::LoadStatus::copyFail:
          std::cerr << "Error: "
                    << query::describe_sqlite_failure (
                           loadStatus.rc.value(), "load database",
                           loadStatus.msg
                       )
                    << std::endl;
          return EXIT_FAILURE;
        case PileupDB::LoadStatus::contentCorrupt:
          std::cerr << fmt::format (
                           "Error: database at {} appears to be "
                           "corrupt:\n{}",
                           args.dbPath.value(), loadStatus.msg.value()
                       )
                    << std::endl;
          return EXIT_FAILURE;
        case PileupDB::LoadStatus::schemaMismatch:
          std::cerr << fmt::format (
                           "Error: database at {} does not have the "
                           "expected schema for an apb database. Is "
                           "it "
                           "from an old version?",
                           args.dbPath.value()
                       )
                    << std::endl;
          return EXIT_FAILURE;
        case PileupDB::LoadStatus::success:
          break;
      }
      break;
    case ApbMode::UNSET:
      APB_UNREACHABLE ("CLI code malformed; unknown mode");
  }


  if (args.dumpPath) {
    switch (const auto dumpStatus =
                query::dump_to_disk (db, args.dumpPath.value().string());
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
      db, schema::UserReadViewSelect::sqlPrefix, {}
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
  auto* startupStmt = *prepResult;
  *prepResult = nullptr;
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
  // FIXME: don't do that. Use global state.
  AppState state{
      .db = {
          .db = std::move (db),
          .userClause = {},
          .selectStmt = startupStmt,
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
    // before we see them: tb_init calls tb_shutdown() ->
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
        // FIXME: what values of TERM does apb support?? Document.
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
        {.fg = TB_DIM}
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
        // FIXME: hold rather than crash.
        // Requires modification of error handling in widgets.cpp
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
  }
  tb_cleanup.invoke();

  if (verboseLog) {
    std::cerr << fmt::format (
                     "sqlite3 max memory usage: {} bytes",
                     sqlite3_memory_highwater (0)
                 )
              << std::endl;
  }

  std::cerr << "Bye!" << std::endl;

  return EXIT_SUCCESS;
}

// returns expected type/str since any failure
// is a top-level crash which can be fully described
// and should be exited upon.
static std::expected<ApbCliArgs, std::string> setup_cli (
    int argc, char** argv
)
{
  // NOTE: helptext NOT built from
  // CLI; see helptext above. Confirm
  // they match when making changes.

  std::vector<std::string> posArgV;
  ApbCliArgs argsOut;

  auto parseFail = [] (std::string msg) -> std::unexpected<std::string> {
    return std::unexpected (fmt::format ("{}\n{}\n", msg, cliHelp));
  };

  auto getFlagValue = [&] (
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
    else if (arg == "-0" || arg == "--zero-based") {
      argsOut.zeroBased = true;
    }
    else if (arg == "--demo") {
      if (argsOut.mode != ApbMode::UNSET) {
        return parseFail (
            "arguments --db and --demo are mutually exclusive"
        );
      }
      argsOut.mode = ApbMode::demo;
    }
    else if (arg == "--dump") {
      auto val = getFlagValue (i, "--dump");
      if (!val) {
        return parseFail (val.error());
      }
      argsOut.dumpPath = *val;
    }
    else if (arg == "--log") {
      argsOut.verboseLog = true;
    }
    else if (arg == "--db") {
      auto val = getFlagValue (i, "--db");
      if (!val) {
        return parseFail (val.error());
      }
      if (argsOut.mode != ApbMode::UNSET) {
        return parseFail (
            "arguments --db and --demo are mutually exclusive"
        );
      }
      argsOut.mode = ApbMode::db;
      argsOut.dbPath = *val;
    }
    else if (arg == "-r" || arg == "--ref") {
      auto val = getFlagValue (i, "--ref");
      if (!val) {
        return parseFail (val.error());
      }
      argsOut.refPath = *val;
    }
    else if (arg.starts_with ("-")) {
      return parseFail (fmt::format ("unrecognized argument: {}", arg));
    }
    else {
      posArgV.emplace_back (arg);
    }
  }

  if (argsOut.mode == ApbMode::demo) {
    if (argsOut.refPath) {
      return parseFail ("--ref is not valid with --demo");
    }
    if (!posArgV.empty()) {
      return parseFail ("--demo takes no positional arguments");
    }
    if (argsOut.zeroBased) {
      return parseFail ("-0/--zero-based is not valid with --demo");
    }
  }
  else if (argsOut.mode == ApbMode::db) {
    if (!posArgV.empty()) {
      return parseFail ("--db takes no positional arguments");
    }
    if (argsOut.refPath) {
      return parseFail ("--ref is not valid with --db");
    }
    if (argsOut.dumpPath) {
      return parseFail ("--dump is not valid with --db");
    }
    if (argsOut.zeroBased) {
      return parseFail ("-0/--zero-based is not valid with --db");
    }
  }
  else {
    // locus mode
    // FIXME: deduplicate alignments, or crash on duplication
    argsOut.mode = ApbMode::locus;
    if (posArgV.size() < 2) {
      return parseFail (
          "expected LOCUS ALN...\n\t(or pass --demo / --db PATH)"
      );
    }
    argsOut.locus = posArgV[0];

    // label each aln by its shortest distinct trailing path suffix
    std::vector<std::vector<std::filesystem::path>> canon;
    for (size_t i = 1; i < posArgV.size(); ++i) {
      std::error_code ec;
      const auto full = std::filesystem::weakly_canonical (posArgV[i], ec);
      if (ec) {
        return std::unexpected (
            fmt::format ("{}: {}", posArgV[i], ec.message())
        );
      }
      canon.emplace_back (full.begin(), full.end());
      argsOut.alns.push_back ({.path = posArgV[i], .label = {}});
    }
    auto labelAt = [&] (size_t i, size_t depth) {
      std::filesystem::path suffix;
      for (size_t j = canon[i].size() - std::min (depth, canon[i].size());
           j < canon[i].size(); ++j) {
        suffix /= canon[i][j];
      }
      return suffix.generic_string();
    };
    std::vector<size_t> depth (canon.size(), 1);
    for (bool collided = true; collided;) {
      collided = false;
      std::map<std::string, std::vector<size_t>> groups;
      for (size_t i = 0; i < canon.size(); ++i) {
        groups[labelAt (i, depth[i])].push_back (i);
      }
      for (const auto& [label, idxs] : groups) {
        if (idxs.size() < 2) {
          continue;
        }
        collided = true;
        for (const auto i : idxs) {
          // still colliding at full depth; same file
          if (depth[i] >= canon[i].size()) {
            return parseFail (
                fmt::format (
                    "duplicate alignment file: {}", posArgV[i + 1]
                )
            );
          }
          ++depth[i];
        }
      }
    }
    for (size_t i = 0; i < canon.size(); ++i) {
      argsOut.alns[i].label = labelAt (i, depth[i]);
    }
  }

  return argsOut;
}


struct PileupCapture {
  htsFile* br_fh = nullptr;  // borrowed
  hts_itr_t* o_it = nullptr;
};
extern "C" {
int pileup_func (void* br_data, bam1_t* br_b)
{
  const PileupCapture* br_d = (PileupCapture*)(br_data);
  // No filtering
  return sam_itr_next (br_d->br_fh, br_d->o_it, br_b);
}
}

// separated into fn for readability
// returns expected void/str since any failure
// is a top-level crash which can be fully described
// and exited upon.
static std::expected<void, std::string> populate_db_mode_locus (
    PileupDB& db, const std::vector<ApbCliArgs::AlnInput>& alns,
    const std::string& locus,
    const std::optional<std::filesystem::path>& refPath, bool zeroBased
)
{
  // helper types for this fn
  struct AlnFile {
    htsFile* o_fh = nullptr;
    sam_hdr_t* o_hdr = nullptr;
    hts_idx_t* o_idx = nullptr;
    std::filesystem::path path;
    uint32_t locusTid = UINT32_MAX;

    void destroy() const noexcept
    {
      hts_idx_destroy (o_idx);
      sam_hdr_destroy (o_hdr);
      hts_close (o_fh);
    }
  };
  struct PileupIterator {
    PileupCapture* o_cap = nullptr;
    bam_plp_t o_plp = nullptr;
    const bam_pileup1_t* br_plpArr = nullptr;
    size_t nPlp = 0;

    void destroy() noexcept
    {
      if (o_cap != nullptr) {
        hts_itr_destroy (o_cap->o_it);
        delete o_cap;
      }
      bam_plp_destroy (o_plp);
      br_plpArr = nullptr;
    }
  };

  // convert locus from string
  std::string locusContigName;
  hts_pos_t locusPos;
  const auto locusStrSepPos = locus.find_last_of (':');
  if (locusStrSepPos == 0) {
    return std::unexpected (
        "Invalid locus specifier; no contig before colon separator"
    );
  }
  if (locusStrSepPos == locus.length() - 1) {
    return std::unexpected (
        "Invalid locus specifier; no coordinate after colon separator"
    );
  }
  if (locusStrSepPos >= locus.length()) {
    return std::unexpected (
        "Invalid locus specifier; no colon separator present"
    );
  }
  if (std::all_of (
          begin (locus) + locusStrSepPos + 1, locus.end(), isdigit
      )) {
    try {
      locusPos = std::stoll (locus.substr (locusStrSepPos + 1));
    }
    catch (const std::exception& ex) {
      return std::unexpected (
          fmt::format (
              "Invalid locus specifier; could not convert postion to "
              "64-bit integer ({})",
              ex.what()
          )
      );
    }
  }
  else {
    return std::unexpected (
        "Invalid locus specifier; non-digit [0123456789] character found "
        "in position after colon separator"
    );
  }
  if (locus[0] == '{' && locus[locusStrSepPos - 1] == '}') {
    // discard disambiguating braces
    locusContigName = locus.substr (1, locusStrSepPos - 2);
  }
  else {
    locusContigName = locus.substr (0, locusStrSepPos);
  }

  if (!zeroBased) {
    if (locusPos == 0) {
      return std::unexpected (
          "Invalid locus specifier for one-based input; position is less "
          "than 1"
      );
    }
    locusPos--;
  }

  // load reference if provided
  std::optional<faidx_t*> ff;
  if (refPath) {
    ff = fai_load3_format (
        refPath.value().c_str(), NULL, NULL, 0,
        fai_format_options::FAI_FASTA
    );

    if (ff == nullptr) {
      return std::unexpected (
          fmt::format ("Failed to open reference fasta at {}", *refPath)
      );
    }
  }
  Defer ff_cleanup ([&ff]() {
    if (ff) {
      fai_destroy (ff.value());
    }
  });

  // iterate alignment paths. Open, validate, pileup,
  // transform to db
  bool readsFoundAtLocus = false;
  GenomicSpan maxPileupSpan{
      INT64_MAX, 0
  };  // for fetching appropriate reference slice
  for (const auto& alnIn : alns) {
    AlnFile aln;
    Defer aln_cleanup ([&aln]() { aln.destroy(); });

    aln.path = alnIn.path;
    aln.o_fh = hts_open (aln.path.c_str(), "r");
    if (aln.o_fh == nullptr) {
      return std::unexpected ("Failed to open alignment file");
    }
    aln.o_hdr = sam_hdr_read (aln.o_fh);
    if (aln.o_hdr == nullptr) {
      return std::unexpected (
          "Failed to read alignment file header; is it "
          "corrupt?"
      );
    }
    aln.o_idx = sam_index_load (aln.o_fh, aln.path.c_str());
    if (aln.o_idx == nullptr) {
      return std::unexpected (
          "Failed to load index file for alignment; is the "
          "file indexed?"
      );
    }

    switch (const auto rc =
                sam_hdr_name2tid (aln.o_hdr, locusContigName.c_str());
            rc) {
      case -2:
        APB_UNREACHABLE ("Could not parse header despite successful read");
      case -1:
        // FIXME prompt user for confirmation here
        std::cerr << "Alignment at " << aln.path
                  << " does not contain contig " << locusContigName
                  << ", skipping" << std::endl;
        continue;
      default:
        aln.locusTid = static_cast<uint32_t> (rc);
    };

    /*
      Creates new hts_itr_t each call since in this program there is
      at present only a single call to this function per program instance,
      and even in future there is no expected pattern to loci at which pileups
      might be needed, hence little advantage to keeping the iterator alive between calls.
    */
    PileupIterator pileup;
    Defer pileup_destroy ([&pileup]() { pileup.destroy(); });

    auto* o_alnIter =
        sam_itr_queryi (aln.o_idx, aln.locusTid, locusPos, locusPos + 1);
    if (o_alnIter == NULL) {
      // FIXME: is this (and pileup iterator fail) actually plausible?
      return std::unexpected (
          fmt::format (
              "Failed to create read iterator for alignment at {}",
              aln.path
          )
      );
    }

    pileup.o_cap = new PileupCapture{aln.o_fh, o_alnIter};
    pileup.o_plp = bam_plp_init (pileup_func, pileup.o_cap);
    if (pileup.o_plp == NULL) {
      return std::unexpected (
          fmt::format (
              "Failed to create pileup iterator for alignment at {}",
              aln.path
          )
      );
    }
    bam_plp_set_maxcnt (pileup.o_plp, kMaxReads);

    int64_t plpPos = -1;
    int plpTid = -1;
    int nPlp = -1;
    bool locusFound = false;
    const bam_pileup1_t* br_plpArr;
    while ((br_plpArr =
                bam_plp64_auto (pileup.o_plp, &plpTid, &plpPos, &nPlp)) !=
           0) {
      if (nPlp < 0 || plpTid < 0 || plpPos < 0) {
        // FIXME: what is the error space here? should this be a crash or a skip. read src.
        return std::unexpected (
            fmt::format (
                "Unexpected error encountered when iterating pileup for "
                "alignment at {}",
                aln.path
            )
        );
      }
      if (plpPos < locusPos) {
        continue;  // yet to reach locus
      }
      if (plpPos > locusPos) {
        break;
      }
      // else locus found
      locusFound = true;
      pileup.br_plpArr = br_plpArr;
      pileup.nPlp = static_cast<size_t> (nPlp);
      for (int j = 0; j < nPlp; j++) {
        auto* const b1 = br_plpArr[j].b;
        const auto rStart = b1->core.pos;
        const auto rEnd = rStart + bam_cigar2rlen (
                                       static_cast<int> (b1->core.n_cigar),
                                       bam_get_cigar (b1)
                                   );
        maxPileupSpan.start = std::min (maxPileupSpan.start, rStart);
        maxPileupSpan.end = std::max (maxPileupSpan.end, rEnd);
      }
      break;
    }
    if (!locusFound) {
      std::cerr << "No reads in alignment file at " << aln.path
                << " align to the specified locus, skipping" << std::endl;
      continue;
    }

    readsFoundAtLocus = true;

    // FIXME: a TRY_SQL macro would be great
    sqlite3_stmt* fileInsertStmt;
    if (const auto rc = sqlite3_prepare_v2 (
            db, schema::sqlInsertFile.data(), schema::sqlInsertFile.size(),
            &fileInsertStmt, NULL
        );
        rc != SQLITE_OK) {
      APB_UNREACHABLE (
          fmt::format (
              "failed to prepare file insert statement: {}",
              sqlite3_errmsg (db)
          )
      );
    }
    if (const auto rc = sqlite3_bind_text (
            fileInsertStmt, 1, alnIn.label.c_str(),
            static_cast<int> (alnIn.label.size()), SQLITE_TRANSIENT
        );
        rc != SQLITE_OK) {
      APB_UNREACHABLE (
          fmt::format (
              "failed to bind path to file table insert statement: {}",
              sqlite3_errstr (rc)
          )
      );
    }
    if (const auto rc = sqlite3_step (fileInsertStmt); rc != SQLITE_DONE) {
      APB_UNREACHABLE (
          fmt::format ("File table insert failed: {}", sqlite3_errmsg (db))
      );
    }
    sqlite3_finalize (fileInsertStmt);
    const auto alnID = sqlite3_last_insert_rowid (db);

    const auto get_mtid_name = [&aln] (int mtid) {
      return sam_hdr_tid2name (aln.o_hdr, mtid);
    };
    auto insertStatus = hts2sql::insert_pileup (
        db, pileup.br_plpArr, pileup.nPlp, locusContigName, alnID,
        get_mtid_name
    );
    switch (insertStatus.code) {
      case hts2sql::InsertPileupStatus::success:
        break;
      case hts2sql::InsertPileupStatus::sqlFail:
        return std::unexpected (
            query::describe_sqlite_failure (
                insertStatus.rc.value(), "transform/insert alignment data",
                sqlite3_errmsg (db)
            )
        );
      case hts2sql::InsertPileupStatus::auxParseFail:
        // FIXME: provide qname/read/tag details
        return std::unexpected (
            "Failed to parse aux tag in alignment file. Is aux "
            "data corrupt?"
        );
      default:
        APB_UNREACHABLE ("unrecognised InsertPileupErr code");
    }
  }

  if (readsFoundAtLocus) {
    // Fetch reference and insert metadata
    std::optional<std::string> refSlice;
    if (ff) {
      hts_pos_t regLen;
      // uses closed coordinates, hence -1
      auto* o_fetch = faidx_fetch_seq64 (
          *ff, locusContigName.c_str(), maxPileupSpan.start,
          maxPileupSpan.end - 1, &regLen
      );
      if (o_fetch == NULL) {
        return std::unexpected (
            fmt::format (
                "Failed to fetch reference region from fasta at {} "
                "for span {}:{}-{} (0-based end-exclusive coordinates)",
                refPath.value(), locusContigName, maxPileupSpan.start,
                maxPileupSpan.end
            )
        );
      }
      refSlice = {o_fetch, static_cast<size_t> (regLen)};
      free (o_fetch);
    }

    if (const auto rcInsMeta = hts2sql::insert_metadata (
            db, locusContigName, locusPos, maxPileupSpan, refSlice
        );
        rcInsMeta != SQLITE_OK) {
      return std::unexpected (
          query::describe_sqlite_failure (
              rcInsMeta, "insert locus metadata", sqlite3_errmsg (db)
          )
      );
    }
  }
  else {
    return std::unexpected (
        "No reads at locus in any input alignment file"
    );
  }

  return {};
}
