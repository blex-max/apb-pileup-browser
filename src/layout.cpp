#include "layout.hpp"
#include "tb.hpp"
#include <cassert>

namespace lay {
  using namespace extb;

  Point ClosedBox::get_global
  (Point plocal) {
    return { plocal.x + this->x1, plocal.y + this->y1 };
  }

  Point ClosedBox::get_local
  (Point pglobal) {
    Point plocal;
    if (pglobal.x < this->x1 || pglobal.x > this->x2) {
      plocal.x = -1;
    } else {
      plocal.x = pglobal.x - this->x1;
    }
    if (pglobal.y < this->y1 || pglobal.y > this->y2) {
      plocal.y = -1;
    } else {
      plocal.y = pglobal.y - this->y1;
    }
    return plocal;
  }

  bool ClosedBox::is_in
  (Point pglobal) {
    return get_local(pglobal).valid();
  }

  Point ClosedBox::set_local
  (Point plocal, uint32_t ch, tb_attr fg, tb_attr bg) {
    const auto pglobal = get_global(plocal);
    return set_cell(pglobal, ch, fg, bg);
  }

  void ClosedBox::write_string
  (Point pstart, std::string_view s, size_t nchar, tb_attr fg, tb_attr bg) {
    // TODO check bounds
    assert (pstart.x >= 0);
    assert (pstart.x <= (this->x2 - this->x1));  // local end
    const size_t x_avail = this->x2 - pstart.x;
    const size_t req_chars = (nchar > 0) ? std::min({nchar, s.size()}) : s.size();
    const auto lim = std::min({x_avail, req_chars});
    for (size_t i = 0; i < lim; ++i) {
      set_local (
        {static_cast<int> (pstart.x + i), pstart.y},
        s[i],
        fg,
        bg
      );
    }
  }
}
