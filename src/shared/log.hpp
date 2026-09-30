#pragma once

#include <fmt/format.h>

#include <optional>
#include <string>
#include <string_view>

namespace apb_log {

inline constexpr std::string_view crashLogPath = "apb-crash-log.txt";

// pairs each of `names` (as captured by #__VA_ARGS__, e.g. "a, b") with
// its formatted value, joined as "a: 1, b: 2"
template <typename... Args>
std::string format_vars (std::string_view names, const Args&... values)
{
  std::string out;
  size_t pos = 0;
  auto appendNext = [&] (const auto& value) {
    auto comma = names.find (',', pos);
    auto name = names.substr (
        pos, comma == std::string_view::npos ? std::string_view::npos
                                             : comma - pos
    );
    while (!name.empty() && name.front() == ' ') {
      name.remove_prefix (1);
    }
    if (!out.empty()) {
      out += ", ";
    }
    out += fmt::format ("{}: {}", name, value);
    pos = (comma == std::string_view::npos) ? names.size() : comma + 1;
  };
  (appendNext (values), ...);
  return out;
}

// -- logging macros -- //

#define APB_VAR_STR(...) \
  apb_log::format_vars (#__VA_ARGS__ __VA_OPT__ (, ) __VA_ARGS__)

#define APB_LOG_VAR(...) APB_LOGD (APB_VAR_STR (__VA_ARGS__))

#define APB_LOG_FN(msg)                                        \
  do {                                                         \
    apb_log::log_push (fmt::format ("{}: {}", __func__, msg)); \
  } while (false)

#define APB_LOG_FN_ENTRY() APB_LOG_FN ("entry")

#define APB_LOG(msg)         \
  do {                       \
    apb_log::log_push (msg); \
  } while (false)

// -- logger handling -- //

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
