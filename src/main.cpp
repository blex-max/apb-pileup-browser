#include <cstdlib>
#include <iostream>

#include <plog/Log.h>
#include <plog/Initializers/RollingFileInitializer.h>

#include "cli.hpp"
#include "core/PileupDB.hpp"
#include "demo.hpp"

int main (int argc, char** argv) {

  auto argRet = init_cli(argc, argv);
  if (!argRet) {
    std::cerr << argRet.error().msg << std::endl;
    return EXIT_FAILURE;
  }
  StartupArgs args = std::move(*argRet);

  plog::init (plog::debug, args.logPath.c_str(), 10000000 /* 10mb limit */, 1);
  PLOGD << "Startup";

  PLOGD << "Creating database";
  auto dbRet = make_db();
  if (!dbRet) {
    std::cerr << dbRet.error().msg << std::endl;
    return EXIT_FAILURE;
  }
  PileupDB db = std::move(*dbRet);
  PLOGD << "Database created";
  if (args.demo) {
    PLOGD << "Inserting demo data into pileup db";
    auto demoRet = insert_demo_data (db, 300, 100);
    if (!demoRet) {
      std::cerr << demoRet.error().msg << std::endl;
      return EXIT_FAILURE;
    }
  } else {
    PLOGD << "Inserting alignment pileup into pileup db";
    // TODO: move open files here?
    auto transRet = insert_pileup (db, *(args.aln), *(args.start));
    if (!transRet) {
      std::cerr << transRet.error().msg << std::endl;
      return EXIT_FAILURE;
    }
  }
  PLOGD << "Insertion complete";

  if (!args.dumpPath.empty()) {
    PLOGD << "Dumping db";
    auto dumpRet = dump_to_disk(db, args.dumpPath);
    if (!dumpRet) {
      std::cerr << dumpRet.error().msg << std::endl;
      return EXIT_FAILURE;
    }
  } else {
    // load frontend
  }

  return EXIT_SUCCESS;

}
