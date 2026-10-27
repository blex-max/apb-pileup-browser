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

#include "app/describe.hpp"
#include "app/dump.hpp"
#include "app/ingest.hpp"
#include "data/hts_sql.hpp"
#include "shared/apb_assert.hpp"
#include "shared/cleanup.hpp"
#include "shared/stringify.hpp"
#include "shared/version.hpp"
#include "tui/init.hpp"
#include "tui/manual.hpp"

// NOTE: helptext is not constructed from
// CLI definition. Must regularly check they
// have not drifted.
// clang-format off
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
  -r, --ref FASTA     reference fasta; TUI will display
                      divergence from reference in reads
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
  --log               print extra startup/shutdown diagnostics
                      to stderr.
  -0, --zero-based    treat LOCUS as 0-based (e.g. from a BED
                      file) instead of 1-based. Not valid
                      with --demo/--db.
  --no-limit-mem      Remove the default limits of ~)txt" STR(GB_LIMIT) R"txt(Gb max
                      pileup size and )txt" STR(DEPTH_LIMIT) R"txt( reads per file.
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
// clang-format on

enum class ApbMode : uint8_t { locus, demo, db, UNSET };
struct ApbCliArgs {
  ApbMode mode = ApbMode::UNSET;
  std::string locus;
  std::vector<std::filesystem::path> alnPaths;

  std::optional<std::filesystem::path> refPath = std::nullopt;
  std::optional<std::filesystem::path> dumpPath = std::nullopt;
  std::optional<std::filesystem::path> dbPath = std::nullopt;

  bool verboseLog = false;
  bool zeroBased = false;
  bool limitHeapSize = true;
};

static std::expected<ApbCliArgs, std::string> parse_args (
    int argc, char** argv
);


int main (int argc, char** argv)
{
  /* setup */
  auto argRet = parse_args (argc, argv);
  if (!argRet) {
    std::cerr << argRet.error() << std::endl;
    return EXIT_FAILURE;
  }
  const ApbCliArgs& args = *argRet;
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

  sqlite3* conn;
  Defer db_shutdown ([&conn]() { sqlite3_close_v2 (conn); });

  switch (args.mode) {
    case ApbMode::locus:
      APB_ASSERT (!args.locus.empty());
      APB_ASSERT (!args.alnPaths.empty());
      apb_init_db (conn);
      if (const auto ret = ingest_sam_auto (
              conn, args.alnPaths, args.locus, args.refPath,
              args.zeroBased, args.limitHeapSize
          );
          !ret) {
        std::cerr << "Error: " << ret.error() << std::endl;
        return EXIT_FAILURE;
      }
      break;
    case ApbMode::demo: {
      apb_init_db (conn);
      ingest_demo_auto (conn);
      break;
    }
    case ApbMode::db: {
      // FIXME: to reinstate
      // APB_ASSERT (args.dbPath);
      // APB_ASSERT (!args.dbPath.value().empty());
      // auto loadRet =
      //     setup_db::load_from_disk (args.dbPath.value().string());
      // if (!loadRet) {
      //   const auto& loadErr = loadRet.error();
      //   switch (loadErr.code) {
      //     case setup_db::LoadErr::openFail:
      //       std::cerr << fmt::format (
      //                        "Error: failed to open database at {}, "
      //                        "reporting error: {}; and extended status "
      //                        "msg: "
      //                        "{}",
      //                        args.dbPath.value(),
      //                        sqlite3_errstr (loadErr.rc.value()),
      //                        loadErr.msg.value()
      //                    )
      //                 << std::endl;
      //       return EXIT_FAILURE;
      //     case setup_db::LoadErr::copyFail:
      //       std::cerr << "Error: "
      //                 << setup_db::describe_sqlite_failure (
      //                        loadErr.rc.value(), "load database",
      //                        loadErr.msg
      //                    )
      //                 << std::endl;
      //       return EXIT_FAILURE;
      //     case setup_db::LoadErr::contentCorrupt:
      //       std::cerr << fmt::format (
      //                        "Error: database at {} appears to be "
      //                        "corrupt:\n{}",
      //                        args.dbPath.value(), loadErr.msg.value()
      //                    )
      //                 << std::endl;
      //       return EXIT_FAILURE;
      //     case setup_db::LoadErr::schemaMismatch:
      //       std::cerr << fmt::format (
      //                        "Error: database at {} does not have the "
      //                        "expected schema for an apb database. Is "
      //                        "it from an old version?",
      //                        args.dbPath.value()
      //                    )
      //                 << std::endl;
      //       return EXIT_FAILURE;
      //   }
      // }
      // g_db::conn = loadRet.value();
      // // load_from_disk only checks the metadata row exists
      // if (!sql::get_locus_data (g_db::conn).valid()) {
      //   std::cerr << fmt::format (
      //                    "Error: database at {} appears to be "
      //                    "corrupt:\nlocus metadata in database is "
      //                    "invalid or corrupt.",
      //                    args.dbPath.value()
      //                )
      //             << std::endl;
      //   return EXIT_FAILURE;
      // }
      // break;
    }
    case ApbMode::UNSET:
      APB_UNREACHABLE ("CLI code malformed; mode unset");
  }

  if (args.dumpPath) {
    if (const auto rc =
            dump_to_disk (conn, args.dumpPath.value().string());
        rc != SQLITE_OK) {
      std::cerr << "Error: "
                << apb_describe_sqlite_failure (
                       "Failed to dump database to disk", rc,
                       sqlite3_errmsg (conn)
                   )
                << std::endl;
      return EXIT_FAILURE;
    }
    // dump succeeded, don't launch TUI.
    return EXIT_SUCCESS;
  }

  run_tui (conn);
  return EXIT_SUCCESS;
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
        argsOut.verboseLog = true;
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
        argsOut.limitHeapSize = false;
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
