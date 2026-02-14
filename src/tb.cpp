#include <cassert>

#include "tb.hpp"

namespace extb {

int set_cell (Point p, uint32_t ch, tb_attr fg, tb_attr bg) {
  if (bg == AS_FG) {
    bg = fg;
  }
  return tb_set_cell(p.x, p.y, ch, fg, bg);
};

// TODO the above is a bit clunky and it would be nice to
// have something more flexible and intuitive
// so: ops to modify attribute bits for a given cell
int set_attr (Point p, tb_attr attr, bool fg, bool bg) {
  tb_cell *c;
  const auto rc = tb_get_cell(p.x, p.y, 1, &c);  // get from back buffer
  if (rc != 0) {
    return rc;
  }
  const tb_attr fg_attr = fg ? attr : c->fg;
  const tb_attr bg_attr = bg ? attr : c->bg;
  return tb_set_cell(p.x, p.y, c->ch, fg_attr, bg_attr);
};

int add_attr (Point p, tb_attr attr, bool fg, bool bg) {
  tb_cell *c;
  const auto rc = tb_get_cell(p.x, p.y, 1, &c);  // get from back buffer
  if (rc != 0) {
    return rc;
  }
  const tb_attr fg_attr = fg ? (c->fg | attr) : c->fg;
  const tb_attr bg_attr = bg ? (c->bg | attr) : c->bg;
  return tb_set_cell(p.x, p.y, c->ch, fg_attr, bg_attr);
};

int rm_attr (Point p, tb_attr attr, bool fg, bool bg) {
  tb_cell *c;
  const auto rc = tb_get_cell(p.x, p.y, 1, &c);  // get from back buffer
  if (rc != 0) {
    return rc;
  }
  const tb_attr fg_attr = fg ? (c->fg & ~attr) : c->fg;
  const tb_attr bg_attr = bg ? (c->bg & ~attr) : c->bg;
  return tb_set_cell(p.x, p.y, c->ch, fg_attr, bg_attr);
};

bool check_attr (Point p, tb_attr attr, bool fg, bool bg) {
  tb_cell *c;
  const auto rc = tb_get_cell(p.x, p.y, 1, &c);  // get from back buffer
  if (rc != 0) {
    return rc;
  }
  bool has_fg = fg ? (c->fg & attr) : true;
  bool has_bg = bg ? (c->bg & attr) : true;
  return has_fg & has_bg;
};


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
