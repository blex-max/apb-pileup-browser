#pragma once

#include <utility>

extern "C" {
  #include <termbox2.h>
}

namespace extb {

struct Point {
  int
  x=-1,
  y=-1;

  bool valid () {
    return (x > 0 && y > 0);
  }
};
// pipe syntax
template<typename F>
requires requires(F&& f, Point p) { std::forward<F>(f)(p); }
auto operator|(Point p, F&& f)
  -> decltype(std::forward<F>(f)(p))
{
  return std::forward<F>(f)(p);
}

using tb_attr = unsigned short;

Point set_cell (Point p, uint32_t ch, tb_attr fg=0, tb_attr bg=0);

int mod_cell (Point p, bool opor, tb_attr fg, tb_attr bg);

Point dim (Point p);

}
