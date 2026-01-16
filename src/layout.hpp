#pragma once

namespace lo {

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
  };

}

