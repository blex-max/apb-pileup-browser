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

    // half open ends
    int xend () const noexcept {
      return x2 + 1;
    }
    int yend () const noexcept {
      return y2 + 1;
    }
    int xsz () const noexcept {
      return x2 - x1 + 1;
    }
    int ysz () const noexcept {
      return y2 - y1 + 1;
    }

    extb::Point get_global
    (extb::Point plocal);

    extb::Point get_local
    (extb::Point pglobal);

    bool is_in
    (extb::Point pglobal);

    int set_local
    (extb::Point plocal, uint32_t ch, extb::tb_attr fg=0, extb::tb_attr bg=0);

    // nchar=0 == print as much of string as possible
    int write_string
    (extb::Point start, std::string_view s, size_t nchar=0, extb::tb_attr fg=0, extb::tb_attr bg=0);
  };

}

