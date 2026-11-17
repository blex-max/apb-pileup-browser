#include <fmt/format.h>
#include <fmt/std.h>
#include <htslib/faidx.h>
#include <htslib/hts.h>
#include <htslib/sam.h>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "generic/db_io.hpp"
#include "generic/describe.hpp"
#include "ingest.hpp"
#include "log.hpp"
#include "model/apb_db.hpp"
#include "model/schema.hpp"
#include "tui/manual.hpp"
#include "tui/query.hpp"
#include "tui/run.hpp"
#include "utility/apb_assert.hpp"
#include "utility/cleanup.hpp"
#include "utility/stringify.hpp"
#include "version.hpp"

// NOTE: helptext is not constructed from
// CLI definition. Must regularly check they
// have not drifted.
// clang-format off
static constexpr std::string_view cliHelp =
    R"txt(usage: apb [options] LOCUS ALN...
       apb [options] DB
       apb --demo

 apb is a terminal-based genome browser designed for
 viewing and querying reads at specific loci. It features
 an easy-to-navigate interface and powerful SQL-based query
 syntax.

 Print the manual with `apb --manual` for a complete, concise
 guide to using apb. It is best read with a pager, e.g.
 `apb --manual | less`.

 Type `help` and press enter in the TUI for in-app help.

arguments:
  LOCUS     genomic locus, e.g. chr1:12345 (1-based).
  ALN...    alignment file/s (sam/bam/cram).
OR
  DB        path to a prexisting apb database dump.

options:
  -r, --ref FASTA     reference fasta; TUI will display
                      divergence from reference in reads.
                      (Invalid with DB; apb databases
                      hold the reference slice internally)
  --dump PATH         Dump the database to PATH as an
                      sqlite database dump.
  --no-tui            Build the locus database and exit,
                      without opening the TUI.
                      Ignored without --dump.
  --use-disk          Do not keep the database in memory
                      instead stream to a temporary file
                      or the path provided to --dump and
                      open a connection to that file on
                      disk.
                      If used with DB, connect directly
                      to the database at DB on disk
                      rather than copying into memory.
                      Useful if you are RAM-bound and are
                      loading very large pileups.
                      Implies --no-limit-mem.
  --no-limit-mem      Remove the default limits of ~)txt" STR(GB_LIMIT) R"txt(Gb max
                      pileup size and )txt" STR(DEPTH_LIMIT) R"txt( reads per file
                      when loading into memory.
  -0, --zero-based    treat the LOCUS argument as 0-based
                      (e.g. from a BED file), instead of
                      1-based, at the CLI.
                      The TUI will still show 1-based
                      coordinates.
  --manual            Print the apb manual to stdout and exit.
  --demo              View demo data.
                      Ignores all other arguments except --log.
  --log PATH          Log debug info to PATH.
  -h, --help          show this help message and exit.
  -v, --version       print version information and exit.

 **IMPORTANT**:
  apb displays all coordinate data in
  1-based closed coordinates.

 On startup you will be focused at the in-app
 command prompt at the bottom of the TUI.
 Try typing "help" and submitting with Enter,
 or focus the read viewer with Esc and navigate
 with the arrow keys.
 Press Ctrl-C to quit.

 See README.md for project background and development
 information.
)txt"
"\napb version " APB_VERSION "\n";
// clang-format on

enum class ApbMode : uint8_t { locus, demo, db, UNSET };
struct ApbCliArgs {
  ApbMode mode = ApbMode::UNSET;
  std::string locus;
  std::vector<std::filesystem::path> alnPaths;

  std::optional<std::filesystem::path> refPath = std::nullopt;
  std::optional<std::filesystem::path> dumpPath = std::nullopt;
  std::optional<std::filesystem::path> dbPath = std::nullopt;

  std::optional<std::filesystem::path> logPath = std::nullopt;

  bool zeroBased = false;
  bool limitMemUsage = true;
  bool useDisk = false;

  // for debugging (see parse_args)
  bool autoflush = false;
};

static std::expected<ApbCliArgs, std::string> parse_args (
    int argc, char** argv
);

// Opens a new in-memory apb database into `conn`. Caller must
// close (including on failure). Reports failure to stderr.
static bool init_db (sqlite3*& conn)
{
  int rc = sqlite3_open (":memory:", &conn);
  if (rc == SQLITE_OK) {
    rc = apb_create_schema (conn);
  }
  switch (rc) {
  case SQLITE_OK:
    return true;
    // more cases may be added if they're outside of apb's control
  case SQLITE_NOMEM:
    // FIXME: nomem might be worth failing gracefully on throughout,
    // since running out of memory on a cluster interactive session
    // or head node is not implausible.
    std::cerr << "Error: failed to initialise database BEFORE "
                 "attempting to load pileup with out of memory error. "
                 "Are you in a memory constrained environment?"
              << std::endl;
    return false;
  default:
    APB_UNREACHABLE (
        "failed to initialise apb database", rc, sqlite3_errstr (rc),
        sqlite3_errmsg (conn)
    );
  }
}

int main (int argc, char** argv)
{
  /* setup */
  auto argRet = parse_args (argc, argv);
  if (!argRet) {
    std::cerr << argRet.error() << std::endl;
    return EXIT_FAILURE;
  }
  const ApbCliArgs& args = *argRet;
  apb_log::init (args.logPath, '\n', args.autoflush);
  Defer log_deinit ([]() { apb_log::deinit(); });

  /* config */
  // must configure before sqlite3 is initialised (which will happen
  // when database is initalised).
  if (const auto rc = sqlite3_config (SQLITE_CONFIG_MEMSTATUS, 1);
      rc != SQLITE_OK) {
    apb_log::log_push (
        "Could not enable sqlite3 memory reporting. Memory "
        "stats will be invalid"
    );
  }

  sqlite3* conn = nullptr;
  Defer db_shutdown ([&conn]() { sqlite3_close_v2 (conn); });

  switch (args.mode) {
  case ApbMode::locus: {
    APB_ASSERT (!args.locus.empty());
    APB_ASSERT (!args.alnPaths.empty());
    // FIXME: this is stupid. Should be within the ingest TU,
    // and those fns can all return a conn.
    if (!init_db (conn)) {
      return EXIT_FAILURE;
    }
    /* bounds */
    constexpr size_t kDepthLimit = 500'000;
    constexpr size_t kBytesLimit = static_cast<size_t> (4) * 1'000'000'000;
    if (const auto ret = ingest_sam_auto (
            conn, args.alnPaths, args.locus, args.refPath, args.zeroBased,
            args.limitMemUsage ? std::make_optional (kBytesLimit)
                               : std::nullopt,
            args.limitMemUsage ? std::make_optional (kDepthLimit)
                               : std::nullopt
        );
        !ret) {
      std::cerr << "Error: " << ret.error() << std::endl;
      return EXIT_FAILURE;
    }
    break;
  }
  case ApbMode::demo: {
    if (!init_db (conn)) {
      return EXIT_FAILURE;
    }
    // NOTE: demo always loads db into memory.
    ingest_demo (conn);
    break;
  }
  case ApbMode::db: {
    APB_ASSERT (args.dbPath);
    APB_ASSERT (!args.dbPath.value().empty());
    if (const auto ret = ingest_db_auto (*args.dbPath, args.useDisk);
        !ret) {
      std::cerr << "Error: " << ret.error() << std::endl;
      return EXIT_FAILURE;
    }
    else {
      conn = *ret;
    }
    break;
  }
  case ApbMode::UNSET:
  default:
    APB_UNREACHABLE ("CLI code malformed; mode unset or unknown");
  }

  // FIXME: Don't dump and exit unless --no-tui.
  // FIXME: if --stream
  // open the dump path without ever loading into memory,
  // and in locus mode gently stream the pileup into it.
  // That means ingest pathways need to change.
  // FIXME: only relevant to locus and demo modes.
  if (args.dumpPath) {
    if (const auto ret =
            dump_to_disk (conn, args.dumpPath.value().string());
        !ret) {
      std::cerr << "Error: "
                << apb_describe_sqlite_failure (
                       "Failed to dump database to disk", ret.error().rc,
                       ret.error().dumpConnMsg
                   )
                << std::endl;
      return EXIT_FAILURE;
    }
    // dump succeeded, don't launch TUI.
    return EXIT_SUCCESS;
  }

  return run_tui (conn);
}

// returns expected type/str since any failure
// is a top-level crash which can be fully described
// and should be exited upon.
static std::expected<ApbCliArgs, std::string> parse_args (
    int argc, char** argv
)
{
  // NOTE: helptext NOT built from
  // CLI; see helptext above. Confirm
  // they match when making changes.

  std::vector<std::string> posArgV;
  ApbCliArgs argsOut;

#define EXIT_W_USAGE(_msg) \
  return std::unexpected (fmt::format ("{}\n{}\n", (_msg), cliHelp))


  {
    /* collect and validate inputs */
    int i = 1;
#define OPT_VAL_OR_EXIT(_flag_name, _out_str)                   \
  do {                                                          \
    if (i + 1 >= argc) {                                        \
      EXIT_W_USAGE (                                            \
          fmt::format ("{} requires an argument", (_flag_name)) \
      );                                                        \
    }                                                           \
    (_out_str) = argv[++i];                                     \
  } while (false)
    for (; i < argc; ++i) {
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
        if (argsOut.mode == ApbMode::demo) {
          EXIT_W_USAGE ("--demo specified more than once");
        }
        if (argsOut.mode != ApbMode::UNSET) {
          EXIT_W_USAGE (
              "arguments --db and --demo are mutually exclusive"
          );
        }
        argsOut.mode = ApbMode::demo;
      }
      else if (arg == "--dump") {
        OPT_VAL_OR_EXIT ("--dump", argsOut.dumpPath);
      }
      else if (arg == "--log") {
        OPT_VAL_OR_EXIT ("--log", argsOut.logPath);
      }
      else if (arg == "--autoflush") {
        argsOut.autoflush = true;
      }
      else if (arg == "--db") {
        if (argsOut.mode == ApbMode::db) {
          EXIT_W_USAGE ("--db specified more than once");
        }
        OPT_VAL_OR_EXIT ("--db", argsOut.dbPath);
        if (argsOut.mode != ApbMode::UNSET) {
          EXIT_W_USAGE (
              "arguments --db and --demo are mutually exclusive"
          );
        }
        argsOut.mode = ApbMode::db;
      }
      else if (arg == "-r" || arg == "--ref") {
        OPT_VAL_OR_EXIT ("--ref", argsOut.refPath);
      }
      else if (arg == "--no-limit-mem") {
        argsOut.limitMemUsage = false;
      }
      else if (arg == "--stream") {
        argsOut.useDisk = true;
      }
      else if (arg.starts_with ("-")) {
        EXIT_W_USAGE (fmt::format ("unrecognized argument: {}", arg));
      }
      else {
        // options finished
        break;
      }
    }
#undef OPT_VAL_OR_EXIT
    for (; i < argc; ++i) {
      if (*argv[i] == '-') {
        EXIT_W_USAGE ("option encountered after positional argument");
      }
      posArgV.emplace_back (argv[i]);
    }
  }

  /* validate args */
  if (argsOut.mode == ApbMode::demo) {
    if (argsOut.refPath) {
      EXIT_W_USAGE ("--ref is not valid with --demo");
    }
    if (!posArgV.empty()) {
      EXIT_W_USAGE ("--demo takes no positional arguments");
    }
    if (argsOut.zeroBased) {
      EXIT_W_USAGE ("-0/--zero-based is not valid with --demo");
    }
  }
  else if (argsOut.mode == ApbMode::db) {
    if (!posArgV.empty()) {
      EXIT_W_USAGE ("--db takes no positional arguments");
    }
    if (argsOut.refPath) {
      EXIT_W_USAGE ("--ref is not valid with --db");
    }
    if (argsOut.dumpPath) {
      EXIT_W_USAGE ("--dump is not valid with --db");
    }
    if (argsOut.zeroBased) {
      EXIT_W_USAGE ("-0/--zero-based is not valid with --db");
    }
  }
  else {
    // locus mode
    argsOut.mode = ApbMode::locus;
    if (posArgV.size() < 2) {
      EXIT_W_USAGE (
          "expected LOCUS ALN...\n\t(or pass --demo / --db PATH)"
      );
    }
    if (posArgV.size() > 100) {
      EXIT_W_USAGE ("apb takes a maximum of 99 alignments");
    }
    argsOut.locus = posArgV[0];

    /* --- verify paths --- */
    std::vector<std::filesystem::path> alnUserPaths;
    std::vector<std::filesystem::path> alnAbsPaths;
    for (size_t i = 0; i < posArgV.size() - 1; ++i) {
      std::filesystem::path thisUserPath{posArgV[i + 1]};
      std::filesystem::path thisAbsPath;
      try {
        thisAbsPath = std::filesystem::canonical (thisUserPath);
      }
      catch (const std::exception& ex) {
        // will throw if, for example, path doesn't exist
        return std::unexpected (
            fmt::format (
                "Could not resolve path {} ({})", thisUserPath, ex.what()
            )
        );
      }
      if (std::filesystem::is_directory (thisAbsPath)) {
        return std::unexpected (
            fmt::format ("Path {} resolves to a directory", thisUserPath)
        );
      }
      auto preexistingElem = std::find_if (
          begin (alnAbsPaths), end (alnAbsPaths),
          [&thisAbsPath] (const auto& a) { return a == thisAbsPath; }
      );
      if (preexistingElem != end (alnAbsPaths)) {
        const auto illegalIdx = static_cast<size_t> (
            std::distance (begin (alnAbsPaths), preexistingElem)
        );
        return std::unexpected (
            fmt::format (
                "Input paths {}, {} resolve to the same file! ({})",
                alnUserPaths[illegalIdx], thisUserPath, thisAbsPath
            )
        );
      }
      alnUserPaths.emplace_back (thisUserPath);
      alnAbsPaths.emplace_back (thisAbsPath);
    }
    // we know at this point that all paths are unique paths
    // to existing files (and not directories).
    argsOut.alnPaths = alnUserPaths;
  }

  return argsOut;
#undef EXIT_W_USAGE
}
