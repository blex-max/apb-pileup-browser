#pragma once

#include <cstddef>
#include <ranges>

#include "frontend/extb/extb.hpp"

namespace extb {

struct Span {
  int first = -1, last = -1;
};
inline int first (const Span& s) noexcept { return s.first; };
inline int last (const Span& s) noexcept { return s.last; };
size_t size (const Span& s) noexcept;
bool contains (const Span& s, int p) noexcept;
bool contains (const Span& outer, const Span& inner) noexcept;
Span section (const Span& s, size_t from, size_t to) noexcept;
bool valid (const Span& s) noexcept;

struct JLine {
  int i;
  Span jspan;
};
size_t size (const JLine& l) noexcept;
GlobalCell first (const JLine& l) noexcept;
GlobalCell last (const JLine& l) noexcept;
JLine section (
    const JLine& l, size_t from, size_t to
) noexcept;  // local coordinates
bool valid (const JLine& l) noexcept;

struct ILine {
  Span ispan;
  int j;
};
size_t size (const ILine& l) noexcept;
GlobalCell first (const ILine& l) noexcept;
GlobalCell last (const ILine& l) noexcept;
ILine section (
    const ILine& l, size_t from, size_t to
) noexcept;  // local coordinates
bool valid (const ILine& l) noexcept;

struct Box {
    // default-construct invalid
  Span ispan;
  Span jspan;
};

// box methods
size_t width (const Box& b) noexcept;
size_t height (const Box& b) noexcept;
GlobalCell to_global (const Box& b, LocalCell c);
GlobalCell top_left (const Box& b) noexcept;
GlobalCell top_right (const Box& b) noexcept;
GlobalCell bottom_left (const Box& b) noexcept;
GlobalCell bottom_right (const Box& b) noexcept;
bool valid (const Box& b) noexcept;

// interface for extb
inline auto cell_source (Box box)
{
  const std::size_t width = size (box.jspan);
  const std::size_t count = size (box.ispan) * width;
  return std::views::iota (std::size_t{0}, count) |
         std::views::transform ([box,
                                 width] (std::size_t index) {
           return GlobalCell{
               box.ispan.first +
                   static_cast<int> (index / width),
               box.jspan.first + static_cast<int> (index % width)
           };
         });
}

inline auto cell_source (JLine l)
{
  const std::size_t width = size (l.jspan);
  return std::views::iota (std::size_t{0}, width) |
         std::views::transform ([l] (std::size_t index) {
           return GlobalCell{
               l.i, l.jspan.first + static_cast<int> (index)
           };
         });
}

inline auto cell_source (ILine l)
{
  const std::size_t height = size (l.ispan);
  return std::views::iota (std::size_t{0}, height) |
         std::views::transform ([l] (std::size_t index) {
           return GlobalCell{
               l.ispan.first + static_cast<int> (index), l.j
           };
         });
}

}  // end namespace extb
