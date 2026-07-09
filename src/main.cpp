#include <cstdlib>
#include <iostream>

#include <plog/Log.h>
#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Formatters/TxtFormatter.h>

#include "cli.hpp"
#include "core/PileupDB.hpp"
#include "demo.hpp"

int main (int argc, char** argv) {
  plog::init<plog::TxtFormatter> (plog::debug, plog::streamStdErr);

  PLOGD << "Processing invocation";
  auto argRet = init_cli(argc, argv);
  if (!argRet) {
    std::cerr << argRet.error().msg << std::endl;
    return EXIT_FAILURE;
  }
  StartupArgs args = std::move(*argRet);

  PLOGD << "Creating demo db";
  auto demoRet = make_demo_db(300, 100);
  if (!demoRet) {
    std::cerr << demoRet.error().msg << std::endl;
    return EXIT_FAILURE;
  }
  PileupDB db = std::move(*demoRet);

  PLOGD << "Dumping db";
  auto dumpRet = dump_to_disk(db, "test.db");
  if (!dumpRet) {
    std::cerr << dumpRet.error().msg << std::endl;
    return EXIT_FAILURE;
  }
  

  return EXIT_SUCCESS;

}
