#include "tb.hpp"

namespace extb {

  Point set_cell (Point p, uint32_t ch, tb_attr fg, tb_attr bg) {
    tb_set_cell(p.x, p.y, ch, fg, bg);  // NOTE returns !0 on failure
    return p;
  };

  int mod_cell (Point p, bool opor, tb_attr fg, tb_attr bg) {
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

  Point dim (Point p) {
    mod_cell(p, true, TB_DIM, TB_DIM);
    return p;
  }

  Point clear (Point p) {
    mod_cell(p, false, 0, 0);
    return p;
  }
}
