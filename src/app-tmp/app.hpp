#pragma once

#include "PileupContext.hpp"
#include "hts/types.hpp"

namespace app {

struct StartupArgs {
  PileupPosition start;
  htsacc::AlnFile aln;
  bool demo = false;
};

StartupArgs init_cli (int argc, char** argv);
void init (StartupArgs args);
void loop();
void shutdown();

}  // namespace app
