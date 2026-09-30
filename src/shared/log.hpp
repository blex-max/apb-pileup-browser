#pragma once

#include <optional>
#include <string>
#include <string_view>

// NOTE: not currently used, kept for possible future use
namespace apb_log {

inline constexpr std::string_view crashLogPath = "apb-crash-log.txt";

void init (const std::optional<std::string>& logPath, char sep);

enum class LogStatusCode : uint8_t {
  none,
  flushed,
  msgTooLarge,
  uninit,
};
LogStatusCode log_push (const std::string_view msg, bool writeSep = true);

void dump();

void deinit();

}  // namespace apb_log
