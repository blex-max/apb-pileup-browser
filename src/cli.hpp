#pragma once

#include <expected>
#include <optional>

#include "core/hts_types.hpp"
#include "core/err.hpp"


struct StartupArgs {
    std::optional<PileupPosition> start;
    std::optional<AlnFile> aln;
    bool mode_demo=false;
};

using ArgsOrErr = std::expected<StartupArgs, Err>;
ArgsOrErr init_cli (int argc, char** argv);

