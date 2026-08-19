#pragma once

#include <ranges>

#include "frontend/extb/box/span.hpp"
#include "frontend/extb/extb.hpp"

namespace extb {

// --- TYPES & DECLARATIONS --- //

struct Box {
    // default-construct invalid
    Span xspan;
    Span yspan;
};
int width (const Box& b) noexcept;
int height (const Box& b) noexcept;
/*
box edges & verticies:
  A---B
  |   |
  D---C
*/
GlobalCell vertexA (const Box& b) noexcept;
GlobalCell vertexB (const Box& b) noexcept;
GlobalCell vertexC (const Box& b) noexcept;
GlobalCell vertexD (const Box& b) noexcept;
HLine edgeAB (const Box& b) noexcept;
VLine edgeBC (const Box& b) noexcept;
HLine edgeCD (const Box& b) noexcept;
VLine edgeDA (const Box& b) noexcept;
bool valid (const Box& b) noexcept;
GlobalCell to_global (const Box& b, const LocalCell& c);
bool contains_global (
    const Box& b, const GlobalCell& cglobal
) noexcept;
bool contains_local (
    const Box& b, const LocalCell& clocal
) noexcept;
std::pair<const Span&, const Span&> spans (const Box& b);

// --- END TYPES & DECLARATIONS --- //

// --- CONVERSION TO GlobalCell --- //

// function called internally by extb
// drawing funtions to convert types
// to GlobalCellRange.

GlobalCellRange auto inline cell_source (Box box)
{
  const int width = size (box.xspan);
  const int count = size (box.yspan) * width;
  return std::views::iota (0, count) |
         std::views::transform ([box, width] (int index) {
           return vertexA (box) +
                  dXY (index % width, index / width);
         });
}

// --- END CONVERSION --- //

// --- IMPLEMENTATION --- //

inline int width (const Box& b) noexcept
{
  return size (b.xspan);
}
inline int height (const Box& b) noexcept
{
  return size (b.yspan);
}

inline bool contains_global (
    const Box& b, const GlobalCell& cglobal
) noexcept
{
  return cglobal.x >= b.xspan.first &&
         cglobal.x < b.xspan.last &&
         cglobal.y >= b.yspan.first && cglobal.y < b.yspan.last;
}
inline bool contains_local (
    const Box& b, const LocalCell& clocal
) noexcept
{
  auto wx = width (b);
  auto hy = height (b);
  return valid (clocal) && clocal.x < wx && clocal.y < hy;
}

inline bool valid (const Box& b) noexcept
{
  return valid (b.xspan) && valid (b.yspan);
}

inline GlobalCell to_global (const Box& b, const LocalCell& c)
{
  if (contains_local (b, c)) {
    const auto moved = c + dXY (b.xspan.first, b.yspan.first);
    return {moved.x, moved.y};
  }
  return {};
}

inline GlobalCell vertexA (const Box& b) noexcept
{
  return {b.xspan.first, b.yspan.first};
}

inline GlobalCell vertexB (const Box& b) noexcept
{
  return {b.xspan.last - 1, b.yspan.first};
}

inline GlobalCell vertexC (const Box& b) noexcept
{
  return {b.xspan.first, b.yspan.last - 1};
}

inline GlobalCell vertexD (const Box& b) noexcept
{
  return {b.xspan.last - 1, b.yspan.last - 1};
}

inline HLine edgeAB (const Box& b) noexcept
{
  return {b.xspan, first (b.yspan)};
}

inline HLine edgeCD (const Box& b) noexcept
{
  return {b.xspan, last (b.yspan) - 1};
}

inline VLine edgeDA (const Box& b) noexcept
{
  return {first (b.xspan), b.yspan};
}

inline VLine edgeBC (const Box& b) noexcept
{
  return {last (b.xspan) - 1, b.yspan};
}

inline std::pair<const Span&, const Span&> spans (const Box& b)
{
  return {b.xspan, b.yspan};
}

// --- END IMPLEMENTATION --- //

}  // end namespace extb
