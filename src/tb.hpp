#pragma once

#include <limits>
#include <string_view>

extern "C" {
  #include <termbox2.h>
}

namespace extb {

struct Point {
  int
  x=-1,
  y=-1;

  bool valid () {
    return (x >= 0 && y >= 0);
  }
};
// pipe syntax
// template<typename F>
// requires requires(F&& f, Point p) { std::forward<F>(f)(p); }
// auto operator|(Point p, F&& f)
//   -> decltype(std::forward<F>(f)(p))
// {
//   return std::forward<F>(f)(p);
// }

using tb_attr = unsigned short;
constexpr tb_attr AS_FG = std::numeric_limits<tb_attr>::max();

int set_cell
(Point p, uint32_t ch, tb_attr fg=0, tb_attr bg=AS_FG);

int mod_cell
(Point p, bool opor, tb_attr fg, tb_attr bg=AS_FG);

int write_string
(Point start, std::string_view s, size_t nchar, tb_attr fg=0, tb_attr bg=AS_FG);

}
