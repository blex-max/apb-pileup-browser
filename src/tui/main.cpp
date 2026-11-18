#include <fmt/format.h>
#include <fmt/std.h>
#include <htslib/faidx.h>
#include <htslib/hts.h>
#include <htslib/sam.h>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
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
#include <system_error>
#include <unordered_set>
#include <variant>
#include <vector>

#include "ingest.hpp"
#include "log.hpp"
#include "tui/manual.hpp"
#include "tui/run.hpp"
#include "utility/apb_assert.hpp"
#include "utility/cleanup.hpp"
#include "utility/stringify.hpp"
#include "version.hpp"

// max pileup size, to prevent accidentally loading
// some RAM-destroying pileup.
#define DEPTH_LIMIT 500'000
#define GB_LIMIT 4

// NOTE: helptext is not constructed from
// CLI definition. Must regularly check they
// have not drifted.
// clang-format off
static constexpr std::string_view cliHelp =
    R"txt(usage: apb [options] LOCUS ALN...
       apb [options] DB
       apb [options] --demo

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
  DB        path to a preexisting apb database dump.

options:
  -r, --ref FASTA         reference fasta; TUI will display
                          divergence from reference in reads.
                          Invalid with DB; apb databases
                          hold the reference slice internally.
  -u, --use-disk          If used with LOCUS ALN... or --demo,
                          do not keep the database in memory.
                          Instead stream to a temporary file
                          and open a connection to that file
                          on disk. The file is created under
                          TMPDIR if set, and deleted on exit.
                          If used with DB, connect directly
                          to the database at DB on disk
                          rather than copying into memory.
                          Useful if you are RAM-bound and are
                          loading very large pileups.
                          Invalid with --dump.
  -d, --dump PATH         Dump the database to PATH as an
                          sqlite database dump for use
                          outside of apb, and exit.
                          PATH must not already exist.
                          Invalid with DB and --use-disk.
  --no-limit-mem          Remove the default limits of ~)txt" STR(GB_LIMIT) R"txt(Gb max
                          pileup size and )txt" STR(DEPTH_LIMIT) R"txt( reads per file.
                          Limits apply with --use-disk and
                          --dump also, since reads are held in
                          memory while loading.
                          Invalid with DB and --demo.
  -0, --zero-based        treat the LOCUS argument as 0-based
                          (e.g. from a BED file), instead of
                          1-based, at the CLI.
                          The TUI will still show 1-based
                          coordinates.
                          Invalid with DB and --demo.
  --manual                Print the apb manual to stdout and exit.
  --demo                  View demo data.
                          Invalid with all other arguments
                          except --use-disk, --dump and --log.
  --log PATH              Log debug info to PATH.
  -h, --help              show this help message and exit.
  -v, --version           print version information and exit.

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

struct cli_args_t {
  // per-mode args
  struct locus_args_t {
    std::string locus;
    std::vector<std::filesystem::path> alnPaths;
    std::optional<std::filesystem::path> refPath = std::nullopt;
    bool zeroBased = false;
    bool limitMemUsage = true;
    db_target_t newDb;  // file target means --dump
  };
  struct db_args_t {
    std::filesystem::path path;
    bool useDisk = false;
  };
  struct demo_args_t {
    db_target_t newDb;  // file target means --dump
  };

  std::variant<locus_args_t, db_args_t, demo_args_t> mode;

  std::optional<std::filesystem::path> logPath = std::nullopt;
  // for debugging (see parse_args)
  bool autoflush = false;
};

// I forgot how much I hate std::variant.
// FIXME: a better name than Overloaded, in snake_case
template <class... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};

static std::expected<cli_args_t, std::string> parse_args (
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
  const cli_args_t& args = *argRet;
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

  const auto ingestRet = std::visit (
      Overloaded{
          [] (const cli_args_t::locus_args_t& a) {
            APB_ASSERT (!a.locus.empty());
            APB_ASSERT (!a.alnPaths.empty());
            /* bounds */
            constexpr size_t kDepthLimit = DEPTH_LIMIT;
            constexpr size_t kBytesLimit =
                static_cast<size_t> (GB_LIMIT) * 1'000'000'000;
            return ingest_sam_auto (
                a.newDb, a.locus, a.zeroBased, a.alnPaths, a.refPath,
                a.limitMemUsage ? std::make_optional (kBytesLimit)
                                : std::nullopt,
                a.limitMemUsage ? std::make_optional (kDepthLimit)
                                : std::nullopt
            );
          },
          [] (const cli_args_t::db_args_t& a) {
            return ingest_db_auto (a.path, a.useDisk);
          },
          [] (const cli_args_t::demo_args_t& a) {
            return ingest_demo (a.newDb);
          },
      },
      args.mode
  );
  if (!ingestRet) {
    std::cerr << "Error: " << ingestRet.error() << std::endl;
    return EXIT_FAILURE;
  }
  conn = *ingestRet;

  const bool dumped = std::visit (
      Overloaded{
          [] (const cli_args_t::db_args_t&) { return false; },
          [] (const auto& a) { return a.newDb.kind == db_target_t::file; },
      },
      args.mode
  );
  if (dumped) {
    // db was built at the dump path; don't launch TUI.
    return EXIT_SUCCESS;
  }

  return run_tui (conn);
}

// returns expected type/str since any failure
// is a top-level crash which can be fully described
// and should be exited upon.
static std::expected<cli_args_t, std::string> parse_args (
    int argc, char** argv
)
{
  // NOTE: helptext NOT built from
  // CLI; see helptext above. Confirm
  // they match when making changes.

  cli_args_t argsOut;

  // collected before mode is known
  std::vector<std::string> posArgV;
  bool demo = false;
  bool zeroBased = false;
  bool useDisk = false;
  bool limitMemUsage = true;
  std::optional<std::filesystem::path> refPath = std::nullopt;
  std::optional<std::filesystem::path> dumpPath = std::nullopt;

#define EXIT_W_USAGE(_msg) \
  return std::unexpected (fmt::format ("{}\n{}\n", (_msg), cliHelp))


  {
    /* collect and validate inputs */
    int i = 1;
// takes the next token as the value; rejects one that looks like a flag
#define OPT_VAL_OR_EXIT(_flag_name, _out_str)                   \
  do {                                                          \
    if (i + 1 >= argc || *argv[i + 1] == '-') {                 \
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
        zeroBased = true;
      }
      else if (arg == "--demo") {
        demo = true;
      }
      else if (arg == "-u" || arg == "--use-disk") {
        useDisk = true;
      }
      else if (arg == "-d" || arg == "--dump") {
        OPT_VAL_OR_EXIT ("--dump", dumpPath);
      }
      else if (arg == "--log") {
        OPT_VAL_OR_EXIT ("--log", argsOut.logPath);
      }
      else if (arg == "--autoflush") {
        /* hidden option to enable immediate
        write on logging for live trace */
        argsOut.autoflush = true;
      }
      else if (arg == "-r" || arg == "--ref") {
        OPT_VAL_OR_EXIT ("--ref", refPath);
      }
      else if (arg == "--no-limit-mem") {
        limitMemUsage = false;
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
  if (dumpPath && useDisk) {
    EXIT_W_USAGE ("--dump is not valid with --use-disk");
  }
  db_target_t newDb;
  if (dumpPath) {
    newDb = {.kind = db_target_t::file, .path = *dumpPath};
  }
  else if (useDisk) {
    newDb.kind = db_target_t::tempFile;
  }

  if (demo) {
    if (refPath) {
      EXIT_W_USAGE ("--ref is not valid with --demo");
    }
    if (!posArgV.empty()) {
      EXIT_W_USAGE ("--demo takes no positional arguments");
    }
    if (zeroBased) {
      EXIT_W_USAGE ("-0/--zero-based is not valid with --demo");
    }
    if (!limitMemUsage) {
      EXIT_W_USAGE ("--no-limit-mem is not valid with --demo");
    }
    argsOut.mode = cli_args_t::demo_args_t{.newDb = newDb};
  }
  else if (posArgV.empty()) {
    EXIT_W_USAGE ("expected LOCUS ALN... or DB (or pass --demo)");
  }
  else if (posArgV.size() == 1) {
    if (refPath) {
      EXIT_W_USAGE ("--ref is not valid with DB");
    }
    if (dumpPath) {
      EXIT_W_USAGE ("--dump is not valid with DB");
    }
    if (zeroBased) {
      EXIT_W_USAGE ("-0/--zero-based is not valid with DB");
    }
    if (!limitMemUsage) {
      EXIT_W_USAGE ("--no-limit-mem is not valid with DB");
    }

    const std::filesystem::path dbUserPath{posArgV[0]};
    if (std::error_code ec; !std::filesystem::exists (dbUserPath, ec)) {
      return std::unexpected (
          fmt::format (
              "No database found at {} (to load a locus, pass LOCUS "
              "ALN...)",
              dbUserPath
          )
      );
    }
    if (std::error_code ec;
        std::filesystem::is_directory (dbUserPath, ec)) {
      return std::unexpected (
          fmt::format ("Path {} resolves to a directory", dbUserPath)
      );
    }
    argsOut.mode =
        cli_args_t::db_args_t{.path = dbUserPath, .useDisk = useDisk};
  }
  else {
    if (posArgV.size() > 100) {
      EXIT_W_USAGE ("apb takes a maximum of 99 alignments");
    }

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
    argsOut.mode = cli_args_t::locus_args_t{
        .locus = posArgV[0],
        .alnPaths = alnUserPaths,
        .refPath = refPath,
        .zeroBased = zeroBased,
        .limitMemUsage = limitMemUsage,
        .newDb = newDb,
    };
  }

  return argsOut;
#undef EXIT_W_USAGE
}
