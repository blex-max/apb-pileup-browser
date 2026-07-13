#include <cassert>
#include <format>

template <typename... Args>
std::runtime_error format_runtime_error(
    std::format_string<Args...> fmt, Args&&... args
)
{
  return std::runtime_error(
      std::format(fmt, std::forward<Args>(args)...)
  );
}

inline auto genomic_substr(
    size_t gstart, size_t gpos, size_t nchar, std::string_view s
)
{
  assert(gstart <= gpos);
  return s.substr(gpos - gstart, nchar);
}
