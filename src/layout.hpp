#pragma once

#include "tb.hpp"
#include <string_view>

namespace lay {

struct ClosedBox {
  // closed coordinates
  int
  x1=-1,
  x2=-1,
  y1=-1,
  y2=-1;

  // local coordinate ends
  int xlast () const noexcept {
    return x2 - x1;
  }
  int ylast () const noexcept {
    return y2 - y1;
  }

  extb::Point get_global
  (extb::Point plocal);

  extb::Point get_local
  (extb::Point pglobal);

  bool is_in
  (extb::Point pglobal);

  int set_cell
  (extb::Point plocal, uint32_t ch, extb::tb_attr fg=0, extb::tb_attr bg=0);

  int add_attr
  (extb::Point p, extb::tb_attr attr, bool fg=true, bool bg=true);
  int rm_attr
  (extb::Point p, extb::tb_attr attr, bool fg=true, bool bg=true);

  // nchar=0 == print as much of string as possible
  int write_string
  (extb::Point start, std::string_view s, size_t nchar=0, extb::tb_attr fg=0, extb::tb_attr bg=0);
};

}

