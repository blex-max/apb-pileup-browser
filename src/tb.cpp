#include <cassert>

#include "tb.hpp"

namespace extb {

int set_cell (Point p, uint32_t ch, tb_attr fg, tb_attr bg) {
  if (bg == AS_FG) {
    bg = fg;
  }
  return tb_set_cell(p.x, p.y, ch, fg, bg);
};

int mod_cell (Point p, bool opor, tb_attr fg, tb_attr bg) {
  if (bg == AS_FG) {
    bg = fg;
  }
  tb_cell *c;
  int rc;
  rc = tb_get_cell(p.x, p.y, 1, &c);  // get from back buffer
  if (rc != 0) {
    return rc;
  }
  const auto fg_attr = (opor) ? c->fg | fg : c->fg & fg;
  const auto bg_attr = (opor) ? c->bg | bg : c-> bg & bg;
  const auto ch = c->ch;
  return tb_set_cell(p.x, p.y, ch, fg_attr, bg_attr);
}

// returns nchars written
int write_string
(Point pstart, std::string_view s, size_t nchar, tb_attr fg, tb_attr bg) {
  assert (pstart.x >= 0);
  const size_t lim = (nchar > 0) ? std::min({nchar, s.size()}) : s.size();
  int rc = 0;
  for (size_t i = 0; i < lim; ++i) {
    const auto set_rc = set_cell (
      {static_cast<int> (pstart.x + i), pstart.y},
      s[i],
      fg,
      bg
    );
    if (set_rc != 0) {
      return set_rc;
    } else {
      ++rc;
    }
  }
  return rc;
}

}  // end namespace
