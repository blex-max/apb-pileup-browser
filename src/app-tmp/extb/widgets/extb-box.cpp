#include "extb-box.hpp"

namespace extb {
namespace box {

bool contains_global(
    const GlobalBox& b, const GlobalCell& cglobal
) noexcept
{
  return cglobal.i >= b.ispan.first &&
         cglobal.i <= b.ispan.last &&
         cglobal.j >= b.jspan.first && cglobal.j <= b.jspan.last;
}
bool contains_local(
    const GlobalBox& b, const LocalCell& clocal
) noexcept
{
  return valid(clocal) && clocal.i <= last_local(b).i &&
         clocal.j <= last_local(b).j;
}

bool valid(const Span& s) noexcept
{
  return (s.first >= 0 && s.last >= 0 && s.last >= s.first);
}
bool valid(const GlobalBox& b) noexcept
{
  return valid(b.ispan) && valid(b.jspan);
}

// box factories
GlobalBox make_box(
    const GlobalSpan& ispan, const GlobalSpan& jspan
)
{
  return {ispan, jspan};
}
GlobalBox make_row(int i, const GlobalSpan& jspan)
{
  return {{i, i}, jspan};
}
GlobalBox make_col(const GlobalSpan& ispan, int j)
{
  return {ispan, {j, j}};
}

// box methods
int last_local(const GlobalSpan& s) noexcept
{
  return s.last - s.first;
};
LocalCell last_local(const GlobalBox& b) noexcept
{
  return {last_local(b.ispan), last_local(b.jspan)};
}

// Span methods
size_t Span::size() const noexcept
{
  return valid(*this) ? last - first + 1 : 0;
}
bool contains(const Span& s, int p) noexcept
{
  return valid(s) && (p >= s.first && p <= s.last);
}
bool contains(
    const GlobalSpan& outer, const GlobalSpan& inner
) noexcept
{
  return valid(outer) && valid(inner) &&
         (outer.first <= inner.first &&
          outer.last >= inner.last);
}
bool contains(
    const LocalSpan& outer, const LocalSpan& inner
) noexcept
{
  return valid(outer) && valid(inner) &&
         (outer.first <= inner.first &&
          outer.last >= inner.last);
}

GlobalCell to_global(const GlobalBox& b, const LocalCell& c)
{
  if (contains_local(b, c)) {
    return {c.i + b.ispan.first, c.j + b.jspan.first};
  }
  else {
    return {};
  }
}

GlobalCell top_left(const GlobalBox& b) noexcept
{
  return {b.ispan.first, b.jspan.first};
}

GlobalCell top_right(const GlobalBox& b) noexcept
{
  return {b.ispan.first, b.jspan.last};
}

GlobalCell bottom_left(const GlobalBox& b) noexcept
{
  return {b.ispan.last, b.jspan.first};
}

GlobalCell bottom_right(const GlobalBox& b) noexcept
{
  return {b.ispan.last, b.jspan.last};
}

}  // end namespace box
}  // end namespace extb
