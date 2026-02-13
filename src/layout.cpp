#include "layout.hpp"
#include "tb.hpp"
#include <cassert>

namespace lay {

extb::Point ClosedBox::get_global
(extb::Point plocal) {
  return { plocal.x + this->x1, plocal.y + this->y1 };
}

extb::Point ClosedBox::get_local
(extb::Point pglobal) {
  extb::Point plocal;
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
(extb::Point pglobal) {
  return get_local(pglobal).valid();
}

int ClosedBox::set_local
(extb::Point plocal, uint32_t ch, extb::tb_attr fg, extb::tb_attr bg) {
  const auto pglobal = get_global(plocal);
  return set_cell(pglobal, ch, fg, bg);
}

// returns nchars written
int ClosedBox::write_string
(extb::Point pstart, std::string_view s, size_t nchar, extb::tb_attr fg, extb::tb_attr bg) {
  // TODO check bounds
  assert (pstart.x >= 0);
  assert (pstart.x <= (this->x2 - this->x1));  // local end
  const size_t x_avail = this->x2 - pstart.x;
  const size_t req_chars = (nchar > 0) ? std::min({nchar, s.size()}) : s.size();
  const auto lim = std::min({x_avail, req_chars});
  const auto global_start = get_global(pstart);
  return extb::write_string(global_start, s, lim, fg, bg);
}

} // end namespace
