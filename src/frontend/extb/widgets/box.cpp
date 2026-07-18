#include "box.hpp"

namespace extb {

size_t width (const Box& b) noexcept { return size (b.jspan); }
size_t height (const Box& b) noexcept { return size (b.ispan); }

bool contains_global (
    const Box& b, const GlobalCell& cglobal
) noexcept
{
  return cglobal.i >= b.ispan.first &&
         cglobal.i < b.ispan.last &&
         cglobal.j >= b.jspan.first && cglobal.j < b.jspan.last;
}
bool contains_local (
    const Box& b, const LocalCell& clocal
) noexcept
{
  auto wj = width (b);
  auto hi = height (b);
  return valid (clocal) && clocal.i < hi && clocal.j < wj;
}

bool valid (const Span& s) noexcept
{
  return (s.first >= 0 && s.last >= 0 && s.last > s.first);
}
bool valid (const Box& b) noexcept
{
  return valid (b.ispan) && valid (b.jspan);
}

size_t size (const Span& s) noexcept
{
  return valid (s) ? s.last - s.first : 0;
}
bool contains (const Span& s, int p) noexcept
{
  return valid (s) && (p >= s.first && p < s.last);
}
bool contains (const Span& outer, const Span& inner) noexcept
{
  return valid (outer) && valid (inner) &&
         (outer.first <= inner.first &&
          outer.last >= inner.last);
}
Span section (const Span& s, size_t from, size_t to) noexcept
{
  if (!valid (s) || from > to || to > size (s)) {
    return {-1, -1};
  }
  return {
      s.first + static_cast<int> (from),
      s.first + static_cast<int> (to)
  };
}

// JLine / ILine methods
size_t size (const JLine& l) noexcept { return size (l.jspan); }
GlobalCell first (const JLine& l) noexcept
{
  return {l.i, l.jspan.first};
}
GlobalCell last (const JLine& l) noexcept
{
  return {l.i, l.jspan.last - 1};
}
bool valid (const JLine& l) noexcept
{
  return l.i >= 0 && valid (l.jspan);
}
JLine section (const JLine& l, size_t from, size_t to) noexcept
{
  if (!valid (l) || from > to || to > size (l)) {
    return {l.i, Span{}};
  }
  return {
      l.i, Span{
               l.jspan.first + static_cast<int> (from),
               l.jspan.first + static_cast<int> (to)
           }
  };
}

size_t size (const ILine& l) noexcept { return size (l.ispan); }
GlobalCell first (const ILine& l) noexcept
{
  return {l.ispan.first, l.j};
}
GlobalCell last (const ILine& l) noexcept
{
  return {l.ispan.last - 1, l.j};
}
bool valid (const ILine& l) noexcept
{
  return l.j >= 0 && valid (l.ispan);
}
ILine section (const ILine& l, size_t from, size_t to) noexcept
{
  if (!valid (l) || from > to || to > size (l)) {
    return {Span{}, l.j};
  }
  return {
      Span{
          l.ispan.first + static_cast<int> (from),
          l.ispan.first + static_cast<int> (to)
      },
      l.j
  };
}

GlobalCell to_global (const Box& b, const LocalCell& c)
{
  if (contains_local (b, c)) {
    return {c.i + b.ispan.first, c.j + b.jspan.first};
  }
  else {
    return {};
  }
}

GlobalCell top_left (const Box& b) noexcept
{
  return {b.ispan.first, b.jspan.first};
}

GlobalCell top_right (const Box& b) noexcept
{
  return {b.ispan.first, b.jspan.last - 1};
}

GlobalCell bottom_left (const Box& b) noexcept
{
  return {b.ispan.last - 1, b.jspan.first};
}

GlobalCell bottom_right (const Box& b) noexcept
{
  return {b.ispan.last - 1, b.jspan.last - 1};
}

}  // end namespace extb
