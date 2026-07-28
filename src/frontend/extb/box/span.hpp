#pragma once

#include <cstddef>
#include <ranges>

#include "frontend/extb/extb.hpp"

namespace extb {

// --- TYPES & DECLARATIONS --- //

struct Span {
  int first = -1, last = -1;
};
inline int first (const Span& s) noexcept { return s.first; };
inline int last (const Span& s) noexcept { return s.last; };
size_t size (const Span& s) noexcept;
bool contains (const Span& s, int p) noexcept;
bool contains (const Span& outer, const Span& inner) noexcept;
Span section (
    const Span& s, size_t from, size_t to
) noexcept;  // subset by local coordinates
Span body (const Span& s) noexcept;
bool valid (const Span& s) noexcept;

struct ILine {
  Span ispan;
  int j;
};
size_t size (const ILine& l) noexcept;
GlobalCell first (const ILine& l) noexcept;
GlobalCell last (const ILine& l) noexcept;
ILine section (
    const ILine& l, size_t from, size_t to
) noexcept;  // subset by local coordinates
ILine body (const ILine& l) noexcept;
bool valid (const ILine& l) noexcept;

struct JLine {
  int i;
  Span jspan;
};
size_t size (const JLine& l) noexcept;
GlobalCell first (const JLine& l) noexcept;
GlobalCell last (const JLine& l) noexcept;
JLine section (
    const JLine& l, size_t from, size_t to
) noexcept;  // subset by local coordinates
JLine body (const JLine& l) noexcept;
bool valid (const JLine& l) noexcept;

// --- END TYPES & DECLARATIONS --- //

// --- CONVERSIONS TO GlobalCell --- //

// function called internally by extb
// drawing funtions to convert types
// to GlobalCellRange.

GlobalCellRange auto inline cell_source (ILine l)
{
  const std::size_t height = size (l.ispan);
  return std::views::iota (std::size_t{0}, height) |
         std::views::transform ([l] (std::size_t index) {
           return translate (
               first (l), I (static_cast<int> (index))
           );
         });
}

GlobalCellRange auto inline cell_source (JLine l)
{
  const std::size_t width = size (l.jspan);
  return std::views::iota (std::size_t{0}, width) |
         std::views::transform ([l] (std::size_t index) {
           return translate (
               first (l), J (static_cast<int> (index))
           );
         });
}

// --- END CONVERSIONS --- //

// --- IMPLEMENTATION --- //

inline bool valid (const Span& s) noexcept
{
  return (s.first >= 0 && s.last >= 0 && s.last > s.first);
}

inline size_t size (const Span& s) noexcept
{
  return valid (s) ? static_cast<size_t> (s.last - s.first) : 0;
}
inline bool contains (const Span& s, int p) noexcept
{
  return valid (s) && (p >= s.first && p < s.last);
}
inline bool contains (
    const Span& outer, const Span& inner
) noexcept
{
  return valid (outer) && valid (inner) &&
         (outer.first <= inner.first &&
          outer.last >= inner.last);
}
inline Span section (
    const Span& s, size_t from, size_t to
) noexcept
{
  if (!valid (s) || from > to || to > size (s)) {
    return {-1, -1};
  }
  return {
      s.first + static_cast<int> (from),
      s.first + static_cast<int> (to)
  };
}
inline Span body (const Span& s) noexcept
{
  return section (s, 1, size (s) - 1);
}

// JLine / ILine methods
inline size_t size (const JLine& l) noexcept
{
  return size (l.jspan);
}
inline GlobalCell first (const JLine& l) noexcept
{
  return {l.i, l.jspan.first};
}
inline GlobalCell last (const JLine& l) noexcept
{
  return {l.i, l.jspan.last - 1};
}
inline bool valid (const JLine& l) noexcept
{
  return l.i >= 0 && valid (l.jspan);
}
inline JLine section (
    const JLine& l, size_t from, size_t to
) noexcept
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
inline JLine body (const JLine& l) noexcept
{
  return section (l, 1, size (l) - 1);
}

inline size_t size (const ILine& l) noexcept
{
  return size (l.ispan);
}
inline GlobalCell first (const ILine& l) noexcept
{
  return {l.ispan.first, l.j};
}
inline GlobalCell last (const ILine& l) noexcept
{
  return {l.ispan.last - 1, l.j};
}
inline bool valid (const ILine& l) noexcept
{
  return l.j >= 0 && valid (l.ispan);
}
inline ILine section (
    const ILine& l, size_t from, size_t to
) noexcept
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
inline ILine body (const ILine& l) noexcept
{
  return section (l, 1, size (l) - 1);
}

// --- END IMPLEMENTATION --- //

}  // end namespace extb
