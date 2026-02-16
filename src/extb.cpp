#include <cassert>
#include <cstdint>

#include "extb.hpp"

namespace extb {

int set_cell
(Point p, uint32_t ch, tb_attr fg, tb_attr bg) {
  return tb_set_cell(p.x, p.y, ch, fg, bg);
};
int set_cell
(Point p, uint32_t ch, tb_attr attr) {
  return set_cell(p, ch, attr, attr);
};
int set_cell
(Point p, uint32_t ch) {
  return set_cell(p, ch, 0, 0);
};
int set_cell
(Box b, Point plocal, uint32_t ch, tb_attr fg, tb_attr bg) {
  const auto pglobal = get_global (b, plocal);
  return set_cell (pglobal, ch, fg, bg);
}
int set_cell
(Box b, Point plocal, uint32_t ch, tb_attr attr) {
  const auto pglobal = get_global (b, plocal);
  return set_cell (pglobal, ch, attr, attr);
}
int set_cell
(Box b, Point plocal, uint32_t ch) {
  const auto pglobal = get_global (b, plocal);
  return set_cell (pglobal, ch, 0, 0);
}


int set_attr
(Point p, tb_attr attr, bool fg, bool bg) {
  tb_cell *c;
  const auto rc = tb_get_cell(p.x, p.y, 1, &c);  // get from back buffer
  if (rc != TB_OK) {
    return rc;
  }
  const tb_attr fg_attr = fg ? attr : c->fg;
  const tb_attr bg_attr = bg ? attr : c->bg;
  return tb_set_cell(p.x, p.y, c->ch, fg_attr, bg_attr);
};
int set_attr
(Point p, tb_attr attr) {
  return set_attr (p, attr, true, true);
}
int set_attr_fg
(Point p, tb_attr attr) {
  return set_attr (p, attr, true, false);
}
int set_attr_bg
(Point p, tb_attr attr) {
  return set_attr (p, attr, false, true);
}
int set_attr
(Box b, Point plocal, tb_attr attr, bool fg, bool bg) {
  const auto pglobal = get_global (b, plocal);
  return set_attr (pglobal, attr, fg, bg);
}
int set_attr
(Box b, Point plocal, tb_attr attr) {
  const auto pglobal = get_global (b, plocal);
  return set_attr (pglobal, attr, true, true);
}
int set_attr_fg
(Box b, Point plocal, tb_attr attr) {
  const auto pglobal = get_global (b, plocal);
  return set_attr (pglobal, attr, true, false);
}
int set_attr_bg
(Box b, Point plocal, tb_attr attr) {
  const auto pglobal = get_global (b, plocal);
  return set_attr (pglobal, attr, false, true);
}



int add_attr (Point p, tb_attr attr, bool fg, bool bg) {
  tb_cell *c;
  const auto rc = tb_get_cell(p.x, p.y, 1, &c);  // get from back buffer
  if (rc != TB_OK) {
    return rc;
  }
  const tb_attr fg_attr = fg ? (c->fg | attr) : c->fg;
  const tb_attr bg_attr = bg ? (c->bg | attr) : c->bg;
  return tb_set_cell(p.x, p.y, c->ch, fg_attr, bg_attr);
};
int add_attr
(Point p, tb_attr attr) {
  return add_attr (p, attr, true, true);
}
int add_attr_fg
(Point p, tb_attr attr) {
  return add_attr (p, attr, true, false);
}
int add_attr_bg
(Point p, tb_attr attr) {
  return add_attr (p, attr, false, true);
}
int add_attr
(Box b, Point plocal, tb_attr attr, bool fg, bool bg) {
  const auto pglobal = get_global (b, plocal);
  return add_attr (pglobal, attr, fg, bg);
}
int add_attr
(Box b, Point plocal, tb_attr attr) {
  const auto pglobal = get_global (b, plocal);
  return add_attr (pglobal, attr, true, true);
}
int add_attr_fg
(Box b, Point plocal, tb_attr attr) {
  const auto pglobal = get_global (b, plocal);
  return add_attr (pglobal, attr, true, false);
}
int add_attr_bg
(Box b, Point plocal, tb_attr attr) {
  const auto pglobal = get_global (b, plocal);
  return add_attr (pglobal, attr, false, true);
}



int rm_attr (Point p, tb_attr attr, bool fg, bool bg) {
  tb_cell *c;
  const auto rc = tb_get_cell(p.x, p.y, 1, &c);  // get from back buffer
  if (rc != TB_OK) {
    return rc;
  }
  const tb_attr fg_attr = fg ? (c->fg & ~attr) : c->fg;
  const tb_attr bg_attr = bg ? (c->bg & ~attr) : c->bg;
  return tb_set_cell(p.x, p.y, c->ch, fg_attr, bg_attr);
};
int rm_attr
(Point p, tb_attr attr) {
  return rm_attr (p, attr, true, true);
}
int rm_attr_fg
(Point p, tb_attr attr) {
  return rm_attr (p, attr, true, false);
}
int rm_attr_bg
(Point p, tb_attr attr) {
  return rm_attr (p, attr, false, true);
}
int rm_attr
(Box b, Point plocal, tb_attr attr, bool fg, bool bg) {
  const auto pglobal = get_global (b, plocal);
  return rm_attr (pglobal, attr, fg, bg);
}
int rm_attr
(Box b, Point plocal, tb_attr attr) {
  const auto pglobal = get_global (b, plocal);
  return rm_attr (pglobal, attr, true, true);
}
int rm_attr_fg
(Box b, Point plocal, tb_attr attr) {
  const auto pglobal = get_global (b, plocal);
  return rm_attr (pglobal, attr, true, false);
}
int rm_attr_bg
(Box b, Point plocal, tb_attr attr) {
  const auto pglobal = get_global (b, plocal);
  return rm_attr (pglobal, attr, false, true);
}


bool check_attr
(Point p, tb_attr attr, bool fg, bool bg) {
  tb_cell *c;
  const auto rc = tb_get_cell(p.x, p.y, 1, &c);  // get from back buffer
  if (rc != TB_OK) {
    return rc;
  }
  bool has_fg = fg ? (c->fg & attr) : true;
  bool has_bg = bg ? (c->bg & attr) : true;
  return has_fg & has_bg;
};
bool check_attr
(Box b, Point plocal, tb_attr attr, bool fg, bool bg) {
  const auto pglobal = get_global (b, plocal);
  return check_attr (pglobal, attr, fg, bg);
}


// returns nchars written
int write_string
(Point start, size_t nchar, std::string_view s, tb_attr fg, tb_attr bg) {
  assert (start.x >= 0);
  const size_t lim = (nchar > 0) ? std::min({nchar, s.size()}) : s.size();
  int nout = 0;
  for (size_t i = 0; i < lim; ++i) {
    const auto rc = set_cell (
      {static_cast<int> (start.x + i), start.y},
      s[i],
      fg,
      bg
    );
    if (rc != TB_OK) {
      return rc;
    } else {
      ++nout;
    }
  }
  return nout;
}
int write_string
(Point start, size_t nchar, std::string_view s, tb_attr attr) {
  return write_string (start, nchar, s, attr, attr);
}
int write_string
(Point start, size_t nchar, std::string_view s) {
  return write_string (start, nchar, s, 0, 0);
}
int write_string
(Box b, Point local_start, size_t nchar, std::string_view s, tb_attr fg, tb_attr bg) {
  assert (in_bounds(b, local_start));
  const size_t x_avail = b.gx2 - local_start.x;
  const size_t req_chars = (nchar > 0) ? std::min({nchar, s.size()}) : s.size();
  const auto char_lim = std::min({x_avail, req_chars});
  const auto global_start = get_global(b, local_start);
  return write_string(global_start, char_lim, s, fg, bg);
}
int write_string
(Box b, Point local_start, size_t nchar, std::string_view s, tb_attr attr) {
  return write_string (b, local_start, nchar, s, attr, attr);
}
int write_string
(Box b, Point local_start, size_t nchar, std::string_view s) {
  return write_string (b, local_start, nchar, s, 0, 0);
}
int write_string
(Box b, Point local_start, std::string_view s, tb_attr fg, tb_attr bg) {
  return write_string (b, local_start, 0, s, fg, bg);
}
int write_string
(Box b, Point local_start, std::string_view s, tb_attr attr) {
  return write_string (b, local_start, 0, s, attr);
}
int write_string
(Box b, Point local_start, std::string_view s) {
  return write_string (b, local_start, 0, s);
}


Point get_global
(Box b, Point plocal) {
  return { plocal.x + b.gx1, plocal.y + b.gy1 };
}

Point get_local
(Box b, Point pglobal) {
  Point plocal;
  if (pglobal.x < b.gx1 || pglobal.x > b.gx2) {
    plocal.x = -1;
  } else {
    plocal.x = pglobal.x - b.gx1;
  }
  if (pglobal.y < b.gy1 || pglobal.y > b.gy2) {
    plocal.y = -1;
  } else {
    plocal.y = pglobal.y - b.gy1;
  }
  return plocal;
}

bool is_in
(Box b, Point pglobal) {
  const auto plocal = get_local(b, pglobal);
  return in_bounds (b, plocal);
}

bool in_bounds
(Box b, Point plocal) {
  return (plocal.valid() && plocal.x <= b.xlast && plocal.y <= b.ylast);
}

int clear
(Box b, Point plocal) {
  return set_cell (plocal, ' ');
}
int clear
(Box b) {
  for (int x = 0; x <= b.xlast; ++x) {
    for (int y = 0; y <= b.ylast; ++y) {
      const auto rc = set_cell (b, {x, y}, ' ');
      if (rc != TB_OK) {
        return rc;
      };
    }
  }
  return TB_OK;
}

}  // end namespace
