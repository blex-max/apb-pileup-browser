#include <plog/Initializers/RollingFileInitializer.h>
#include <plog/Log.h>

#include <cstdlib>
#include <iostream>
#include <variant>

#include "cli.hpp"
#include "subcommands.hpp"

int main(int argc, char** argv)
{
  auto argRet = parse_args(argc, argv);
  if (!argRet) {
    std::cerr << argRet.error().msg << std::endl;
    return EXIT_FAILURE;
  }
  StartupArgs args = *argRet;

  plog::init(
      plog::debug, args.logPath.c_str(),
      10000000 /* 10mb limit */, 1
  );
  PLOGD << "Startup";

  auto runRet = std::visit(
      [](const auto& modeArgs) { return run_mode(modeArgs); },
      args.subArgs
  );
  if (!runRet) {
    std::cerr << runRet.error().msg << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
