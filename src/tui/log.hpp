#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace apb_log {

inline constexpr std::string_view crashLogPath = "apb-crash-log.txt";

void init (
    const std::optional<std::string>& logPath, char sep, bool autoflush
);

bool log_push (const std::string_view msg, bool writeSep = true);

// attempts to dump logs to disk,
// for use in the event of a crash.
void crash_dump();

void deinit();

}  // namespace apb_log
