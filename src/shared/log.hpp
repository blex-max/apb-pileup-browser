#pragma once

#include <optional>
#include <string>
#include <string_view>


namespace log {

void init (const std::optional<std::string>& path, char sep);

enum class LogStatusCode : uint8_t {
  none,
  flushed,
  msgTooLarge,
  uninit,
};
LogStatusCode push (const std::string_view msg);

void flush();

void deinit();

}  // namespace log
