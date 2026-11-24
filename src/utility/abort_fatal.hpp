#pragma once

#include <concepts>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>

namespace apb_abort_detail {

inline std::string stringify (std::string_view s)
{
  return std::string (s);
}
// templated so pointers can't implicitly convert to bool
template <std::same_as<bool> T>
std::string stringify (T b)
{
  return b ? "true" : "false";
}
template <std::same_as<char> T>
std::string stringify (T c)
{
  return std::string (1, c);
}

template <typename T>
  requires (std::integral<T> && !std::same_as<T, bool> &&
            !std::same_as<T, char>) ||
           std::floating_point<T>
std::string stringify (T v)
{
  return std::to_string (v);
}

// hook: other class/enum types provide a to_string found by ADL
template <typename T>
  requires (std::is_class_v<T> || std::is_enum_v<T>) &&
           (!std::convertible_to<const T&, std::string_view>) &&
           requires (const T& v) { to_string (v); }
std::string stringify (const T& v)
{
  return std::string (to_string (v));
}

}  // namespace apb_abort_detail

[[noreturn]] void apb_abort_fatal_impl (
    const char* file, int line, std::string_view what,
    std::string_view varNames, std::initializer_list<std::string> values
);

// Report a fatal error to stderr and abort. `varNames` (as captured
// by #__VA_ARGS__, e.g. "a, b") is paired with each of `values`,
// appended to the report as "a=1" lines.
//
// call via assert macros (APB_ / TUI_), not directly
template <typename... Args>
[[noreturn]] void apb_abort_fatal (
    const char* file, int line, std::string_view what,
    std::string_view varNames = "", const Args&... values
)
{
  apb_abort_fatal_impl (
      file, line, what, varNames, {apb_abort_detail::stringify (values)...}
  );
}
