#pragma once

#include <cstddef>
#include <ranges>

#include "frontend/extb/box/span.hpp"
#include "frontend/extb/extb.hpp"

namespace extb {

// --- TYPES & DECLARATIONS --- //

struct Box {
    // default-construct invalid
  Span ispan;
  Span jspan;
};
size_t width (const Box& b) noexcept;
size_t height (const Box& b) noexcept;
// compass directions NESW
GlobalCell nw_vertex (const Box& b) noexcept;
GlobalCell ne_vertex (const Box& b) noexcept;
GlobalCell sw_vertex (const Box& b) noexcept;
GlobalCell se_vertex (const Box& b) noexcept;
JLine north_edge (const Box& b) noexcept;
JLine south_edge (const Box& b) noexcept;
ILine west_edge (const Box& b) noexcept;
ILine east_edge (const Box& b) noexcept;
bool valid (const Box& b) noexcept;
GlobalCell to_global (const Box& b, LocalCell c);
bool contains_global (
    const Box& b, const GlobalCell& cglobal
) noexcept;
bool contains_local (
    const Box& b, const LocalCell& clocal
) noexcept;

// --- END TYPES & DECLARATIONS --- //

// --- CONVERSION TO GlobalCell --- //

// function called internally by extb
// drawing funtions to convert types
// to GlobalCellRange.

GlobalCellRange auto inline cell_source (Box box)
{
  const std::size_t width = size (box.jspan);
  const std::size_t count = size (box.ispan) * width;
  return std::views::iota (std::size_t{0}, count) |
         std::views::transform ([box,
                                 width] (std::size_t index) {
           return translate (
               nw_vertex (box),
               {static_cast<int> (index / width),
                static_cast<int> (index % width)}
           );
         });
}

// --- END CONVERSION --- //

// --- IMPLEMENTATION --- //

inline size_t width (const Box& b) noexcept
{
  return size (b.jspan);
}
inline size_t height (const Box& b) noexcept
{
  return size (b.ispan);
}

inline bool contains_global (
    const Box& b, const GlobalCell& cglobal
) noexcept
{
  return cglobal.i >= b.ispan.first &&
         cglobal.i < b.ispan.last &&
         cglobal.j >= b.jspan.first && cglobal.j < b.jspan.last;
}
inline bool contains_local (
    const Box& b, const LocalCell& clocal
) noexcept
{
  auto wj = width (b);
  auto hi = height (b);
  return valid (clocal) && static_cast<size_t> (clocal.i) < hi &&
         static_cast<size_t> (clocal.j) < wj;
}

inline bool valid (const Box& b) noexcept
{
  return valid (b.ispan) && valid (b.jspan);
}

inline GlobalCell to_global (const Box& b, const LocalCell& c)
{
  if (contains_local (b, c)) {
    const auto moved =
        translate (c, {b.ispan.first, b.jspan.first});
    return {moved.i, moved.j};
  }
  else {
    return {};
  }
}

inline GlobalCell nw_vertex (const Box& b) noexcept
{
  return {b.ispan.first, b.jspan.first};
}

inline GlobalCell ne_vertex (const Box& b) noexcept
{
  return {b.ispan.first, b.jspan.last - 1};
}

inline GlobalCell sw_vertex (const Box& b) noexcept
{
  return {b.ispan.last - 1, b.jspan.first};
}

inline GlobalCell se_vertex (const Box& b) noexcept
{
  return {b.ispan.last - 1, b.jspan.last - 1};
}

inline JLine north_edge (const Box& b) noexcept
{
  return {first (b.ispan), b.jspan};
}

inline JLine south_edge (const Box& b) noexcept
{
  return {last (b.ispan) - 1, b.jspan};
}

inline ILine west_edge (const Box& b) noexcept
{
  return {b.ispan, first (b.jspan)};
}

inline ILine east_edge (const Box& b) noexcept
{
  return {b.ispan, last (b.jspan) - 1};
}

// --- END IMPLEMENTATION --- //

}  // end namespace extb
