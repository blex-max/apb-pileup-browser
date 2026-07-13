#include "extb.hpp"

#include <cassert>

namespace extb {

bool valid(const Cell& c) noexcept
{
  return (c.i >= 0 && c.j >= 0);
}

// returns nchars written
size_t write_string(
    const GlobalCell& start, int j_bound, std::string_view s,
    const Style& style
)
{
  if (!valid(start) || s.empty() || j_bound < 0) {
    return 0;
  }

  const auto jlim = std::min<size_t>(
      s.size(), static_cast<size_t>(j_bound - start.j + 1)
  );

  int nout = 0;
  for (size_t j = 0; j < jlim; ++j) {
    const auto rc =
        set(GlobalCell{start.i, static_cast<int>(start.j + j)},
            s[j], style);
    if (rc != TB_OK) {
      break;
    }
    ++nout;
  }
  return nout;
}

}  // end namespace extb
