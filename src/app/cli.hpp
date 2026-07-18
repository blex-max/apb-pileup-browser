#pragma once

#include <expected>
#include <optional>
#include <string>
#include <variant>

#include "shared/err.hpp"

struct AlnModeArgs {
  std::string alnPath;
  std::string
      locus;  // raw locus string (e.g. "chr1:12345"); resolved to a PileupPosition once the alignment file is opened
  std::optional<std::string> dumpPath;
};
struct DbModeArgs {
  std::string dbPath;
};
struct DemoModeArgs {
  std::optional<std::string> dumpPath;
};

using ModalArgs =
    std::variant<AlnModeArgs, DbModeArgs, DemoModeArgs>;
struct StartupArgs {
  ModalArgs subArgs;
  std::string logPath;
};

using ArgsOrErr = std::expected<StartupArgs, Err>;
ArgsOrErr parse_args (int argc, char** argv);
