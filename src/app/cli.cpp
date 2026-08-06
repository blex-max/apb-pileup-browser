#include "cli.hpp"

#include <expected>
#include <sstream>

#include "argparse/argparse.hpp"

// Defined in CMakeLists.txt
#ifndef APB_VERSION
#define APB_VERSION "undef"
#endif

static constexpr char HELPTEXT[] =
    R"txt( apb is an terminal-based genome browser designed for viewing
 and querying pileup loci. It features a REPL-like command
 line and simple SQL-based query syntax.)txt";
static constexpr char EPILOG[] =
    R"txt( See README.md for further info, or use the in-app help
 (type ? and press enter in the TUI).If you don't have
 the readme, it can be written to disk from within the
 app.)txt";

ArgsOrErr parse_args (int argc, char** argv)
{
  argparse::ArgumentParser cli ("apb", APB_VERSION);
  std::string logPath;

  cli.add_description (HELPTEXT);
  cli.add_epilog (EPILOG);

  argparse::ArgumentParser scmdSam ("sam");
  scmdSam.add_description ("read from alignment file");
  scmdSam.add_argument ("SAM").help (
      "path to alignment file in s/b/cram "
      "format"
  );
  scmdSam.add_argument ("locus").help (
      "genomic locus in the form tid:position"
  );
  scmdSam.add_argument ("--ref")
      .help ("path to reference fasta")
      .metavar ("PATH");
  scmdSam.add_argument ("--dump")
      .help (
          "convert pileup to sqlite3 database, "
          "dump to disk, and exit."
      )  // headless mode
      .metavar ("PATH");

  argparse::ArgumentParser scmdDb ("db");
  scmdDb.add_description ("read from database dump");
  scmdDb.add_argument ("DB").help (
      "path to a previously dumped sqlite3 "
      "database file"
  );

  argparse::ArgumentParser scmdDemo ("demo");
  scmdDemo.add_description ("run in demo mode");
  scmdDemo.add_argument ("--dump")
      .help (
          "convert pileup to sqlite3 database, "
          "dump to disk, and exit."
      )  // headless mode
      .metavar ("PATH");

  // shared args
  cli.add_argument ("--log")
      .help ("log debug output to file")
      .nargs (1)
      .metavar ("PATH")
      .store_into (logPath);

  cli.add_subparser (scmdSam);
  cli.add_subparser (scmdDb);
  cli.add_subparser (scmdDemo);

  try {
    cli.parse_args (argc, argv);
  }
  catch (const std::exception& ex) {
    std::ostringstream oss;
    oss << ex.what() << "\n" << cli;
    return std::unexpected (make_cli_err (oss.str()));
  }

  if (cli.is_subcommand_used ("demo")) {
    return StartupArgs{
        DemoModeArgs{scmdDemo.present<std::string> ("--dump")},
        logPath
    };
  }

  if (cli.is_subcommand_used ("db")) {
    return StartupArgs{
        DbModeArgs{scmdDb.get<std::string> ("DB")}, logPath
    };
  }

  if (cli.is_subcommand_used ("sam")) {
    return StartupArgs{
        AlnModeArgs{
            .alnPath = scmdSam.get<std::string> ("SAM"),
            .locus = scmdSam.get<std::string> ("locus"),
            .refPath = scmdSam.present<std::string> ("--ref"),
            .dumpPath = scmdSam.present<std::string> ("--dump")
        },
        logPath
    };
  }

  std::ostringstream oss;
  oss << "a subcommand is required "
         "(sam|db|demo)\n"
      << cli;
  return std::unexpected (make_cli_err (oss.str()));
}
