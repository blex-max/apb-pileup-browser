#include "cli.hpp"

#include <expected>
#include <sstream>

#include "argparse/argparse.hpp"

ArgsOrErr parse_args (int argc, char** argv)
{
  argparse::ArgumentParser cli ("apb", "0.0.0");
  std::string logPath;

  argparse::ArgumentParser scmdSam ("sam");
  scmdSam.add_description ("read from alignment file");
  scmdSam.add_argument ("SAM").help (
      "path to alignment file in s/b/cram "
      "format"
  );
  scmdSam.add_argument ("locus").help (
      "genomic locus in the form tid:position"
  );
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
  // TODO: locus select (in frontend?)
  // scmdDb.add_argument ("locus")
  //   .help ("genomic locus in the form tid:position");

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
        DemoModeArgs{scmdSam.present<std::string> ("--dump")},
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
            scmdSam.get<std::string> ("SAM"),
            scmdSam.get<std::string> ("locus"),
            scmdSam.present<std::string> ("--dump"),
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
