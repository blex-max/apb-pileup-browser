#pragma once

#include <ranges>

#include "frontend/extb/extb.hpp"

namespace extb {

// --- TYPES & DECLARATIONS --- //

// NOTE: size (and Box width/height) return signed int: screen
// geometry in the same coordinate space as Span bounds, never a
// container index.

struct Span {
  int first = -1, last = -1;
};
inline int first (const Span& s) noexcept { return s.first; };
inline int last (const Span& s) noexcept { return s.last; };
int size (const Span& s) noexcept;
bool contains (const Span& s, int p) noexcept;
bool contains (const Span& outer, const Span& inner) noexcept;
Span construct_relative (
    const Span& s, int from, int to
) noexcept;  // subset by local coordinates
Span body (const Span& s) noexcept;
bool valid (const Span& s) noexcept;

struct VLine {
  int x;
  Span yspan;
};
int size (const VLine& l) noexcept;
GlobalCell first (const VLine& l) noexcept;
GlobalCell last (const VLine& l) noexcept;
VLine construct_relative (
    const VLine& l, int from, int to
) noexcept;  // subset by local coordinates
VLine body (const VLine& l) noexcept;
bool valid (const VLine& l) noexcept;

struct HLine {
  Span xspan;
  int y;
};
int size (const HLine& l) noexcept;
GlobalCell first (const HLine& l) noexcept;
GlobalCell last (const HLine& l) noexcept;
HLine construct_relative (
    const HLine& l, int from, int to
) noexcept;  // subset by local coordinates
HLine body (const HLine& l) noexcept;
bool valid (const HLine& l) noexcept;

// --- END TYPES & DECLARATIONS --- //

// --- CONVERSIONS TO GlobalCell --- //

// function called internally by extb
// drawing funtions to convert types
// to GlobalCellRange.

GlobalCellRange auto inline cell_source (VLine l)
{
  const int height = size (l.yspan);
  return std::views::iota (0, height) |
         std::views::transform ([l] (int index) {
           return first (l) + dY (index);
         });
}

GlobalCellRange auto inline cell_source (HLine l)
{
  const int width = size (l.xspan);
  return std::views::iota (0, width) |
         std::views::transform ([l] (int index) {
           return first (l) + dX (index);
         });
}

// --- END CONVERSIONS --- //

// --- IMPLEMENTATION --- //

inline bool valid (const Span& s) noexcept
{
  return (s.first >= 0 && s.last >= 0 && s.last > s.first);
}

inline int size (const Span& s) noexcept
{
  return valid (s) ? s.last - s.first : 0;
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
inline Span construct_relative (
    const Span& s, int from, int to
) noexcept
{
  const auto newStart = s.first + from;
  const auto newEnd = s.first + to;

  if (!valid (s) || newStart < 0 || newEnd <= newStart) {
    return {-1, -1};
  }
  return {newStart, newEnd};
}
inline Span body (const Span& s) noexcept
{
  return construct_relative (s, 1, size (s) - 1);
}

// HLine / VLine methods
inline int size (const HLine& l) noexcept
{
  return size (l.xspan);
}
inline GlobalCell first (const HLine& l) noexcept
{
  return {l.xspan.first, l.y};
}
inline GlobalCell last (const HLine& l) noexcept
{
  return {l.xspan.last - 1, l.y};
}
inline bool valid (const HLine& l) noexcept
{
  return valid (l.xspan) && l.y >= 0;
}
inline HLine construct_relative (
    const HLine& l, int from, int to
) noexcept
{
  return {construct_relative (l.xspan, from, to), l.y};
}
inline HLine body (const HLine& l) noexcept
{
  return construct_relative (l, 1, size (l) - 1);
}

inline int size (const VLine& l) noexcept
{
  return size (l.yspan);
}
inline GlobalCell first (const VLine& l) noexcept
{
  return {l.x, l.yspan.first};
}
inline GlobalCell last (const VLine& l) noexcept
{
  return {l.x, l.yspan.last - 1};
}
inline bool valid (const VLine& l) noexcept
{
  return l.x >= 0 && valid (l.yspan);
}
inline VLine construct_relative (
    const VLine& l, int from, int to
) noexcept
{
  return {l.x, construct_relative (l.yspan, from, to)};
}
inline VLine body (const VLine& l) noexcept
{
  return construct_relative (l, 1, size (l) - 1);
}

// --- END IMPLEMENTATION --- //

}  // end namespace extb
