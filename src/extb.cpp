#include <cassert>
#include <cstdint>

#include "extb.hpp"

namespace extb {

Box Box::make_box(Span i, Span j) {
  assert (i.valid());
  assert (j.valid());
  return {i.first, i.last, j.first, j.last};
}
Box Box::make_row (int i, Span j) {
  return make_box ({i, i}, j);
}
Box Box::make_col (int j, Span i) {
  return make_box (i, {j, j});
}

Box make_sub_box(Box b, Span i, Span j) {
  assert (i.valid());
  assert (j.valid());
  assert (b.ilocal().contains(i));
  assert (b.jlocal().contains(j));
  auto iglobal = i + b.iglobal();
  auto jglobal = j + b.jglobal();
  return Box::make_box (iglobal, jglobal);
}
Box make_sub_row (Box b, int i, Span j) {
  return make_sub_box (b, {i, i}, j);
}
Box make_sub_row (Box b, int i) {
  return make_sub_row (b, i, {0, b.ilocal().last});
}
Box make_sub_col (Box b, int j, Span i) {
  return make_sub_box (b, i, {j, j});
}
Box make_sub_col (Box b, int j) {
  return make_sub_col (b, j, {0, b.jlocal().last});
}


int set_cell
(Cell p, uint32_t ch, tb_attr fg, tb_attr bg) {
  return tb_set_cell(p.j, p.i, ch, fg, bg);
};
int set_cell
(Cell p, uint32_t ch, tb_attr attr) {
  return set_cell(p, ch, attr, attr);
};
int set_cell
(Cell p, uint32_t ch) {
  return set_cell(p, ch, 0, 0);
};
int set_cell
(Box b, Cell plocal, uint32_t ch, tb_attr fg, tb_attr bg) {
  const auto pglobal = local2global (b, plocal);
  return set_cell (pglobal, ch, fg, bg);
}
int set_cell
(Box b, Cell plocal, uint32_t ch, tb_attr attr) {
  const auto pglobal = local2global (b, plocal);
  return set_cell (pglobal, ch, attr, attr);
}
int set_cell
(Box b, Cell plocal, uint32_t ch) {
  const auto pglobal = local2global (b, plocal);
  return set_cell (pglobal, ch, 0, 0);
}
int set_cell
(Box b, uint32_t ch, tb_attr fg, tb_attr bg) {
  for (int i = 0; i <= b.ilocal().last; ++i) {
    for (int j = 0; j <= b.jlocal().last; ++j) {
      const auto rc = set_cell (b, {i, j}, ch, fg, bg);
      if (rc != TB_OK) {
        return rc;
      };
    }
  }
  return TB_OK;
}
int set_cell
(Box b, uint32_t ch, tb_attr attr) {
  return set_cell (b, ch, attr, attr);
}
int set_cell
(Box b, uint32_t ch) {
  return set_cell (b, ch, 0, 0);
}


int set_attr
(Cell p, tb_attr attr, bool fg, bool bg) {
  tb_cell *c;
  const auto rc = tb_get_cell(p.j, p.i, 1, &c);  // get from back buffer
  if (rc != TB_OK) {
    return rc;
  }
  const tb_attr fg_attr = fg ? attr : c->fg;
  const tb_attr bg_attr = bg ? attr : c->bg;
  return tb_set_cell(p.j, p.i, c->ch, fg_attr, bg_attr);
};
int set_attr
(Cell p, tb_attr attr) {
  return set_attr (p, attr, true, true);
}
int set_attr_fg
(Cell p, tb_attr attr) {
  return set_attr (p, attr, true, false);
}
int set_attr_bg
(Cell p, tb_attr attr) {
  return set_attr (p, attr, false, true);
}
int set_attr
(Box b, Cell plocal, tb_attr attr, bool fg, bool bg) {
  const auto pglobal = local2global (b, plocal);
  return set_attr (pglobal, attr, fg, bg);
}
int set_attr
(Box b, Cell plocal, tb_attr attr) {
  const auto pglobal = local2global (b, plocal);
  return set_attr (pglobal, attr, true, true);
}
int set_attr_fg
(Box b, Cell plocal, tb_attr attr) {
  const auto pglobal = local2global (b, plocal);
  return set_attr (pglobal, attr, true, false);
}
int set_attr_bg
(Box b, Cell plocal, tb_attr attr) {
  const auto pglobal = local2global (b, plocal);
  return set_attr (pglobal, attr, false, true);
}



int add_attr (Cell p, tb_attr attr, bool fg, bool bg) {
  tb_cell *c;
  const auto rc = tb_get_cell(p.j, p.i, 1, &c);  // get from back buffer
  if (rc != TB_OK) {
    return rc;
  }
  const tb_attr fg_attr = fg ? (c->fg | attr) : c->fg;
  const tb_attr bg_attr = bg ? (c->bg | attr) : c->bg;
  return tb_set_cell(p.j, p.i, c->ch, fg_attr, bg_attr);
};
int add_attr
(Cell p, tb_attr attr) {
  return add_attr (p, attr, true, true);
}
int add_attr_fg
(Cell p, tb_attr attr) {
  return add_attr (p, attr, true, false);
}
int add_attr_bg
(Cell p, tb_attr attr) {
  return add_attr (p, attr, false, true);
}
int add_attr
(Box b, Cell plocal, tb_attr attr, bool fg, bool bg) {
  const auto pglobal = local2global (b, plocal);
  return add_attr (pglobal, attr, fg, bg);
}
int add_attr
(Box b, Cell plocal, tb_attr attr) {
  const auto pglobal = local2global (b, plocal);
  return add_attr (pglobal, attr, true, true);
}
int add_attr_fg
(Box b, Cell plocal, tb_attr attr) {
  const auto pglobal = local2global (b, plocal);
  return add_attr (pglobal, attr, true, false);
}
int add_attr_bg
(Box b, Cell plocal, tb_attr attr) {
  const auto pglobal = local2global (b, plocal);
  return add_attr (pglobal, attr, false, true);
}



int rm_attr (Cell p, tb_attr attr, bool fg, bool bg) {
  tb_cell *c;
  const auto rc = tb_get_cell(p.j, p.i, 1, &c);  // get from back buffer
  if (rc != TB_OK) {
    return rc;
  }
  const tb_attr fg_attr = fg ? (c->fg & ~attr) : c->fg;
  const tb_attr bg_attr = bg ? (c->bg & ~attr) : c->bg;
  return tb_set_cell(p.j, p.i, c->ch, fg_attr, bg_attr);
};
int rm_attr
(Cell p, tb_attr attr) {
  return rm_attr (p, attr, true, true);
}
int rm_attr_fg
(Cell p, tb_attr attr) {
  return rm_attr (p, attr, true, false);
}
int rm_attr_bg
(Cell p, tb_attr attr) {
  return rm_attr (p, attr, false, true);
}
int rm_attr
(Box b, Cell plocal, tb_attr attr, bool fg, bool bg) {
  const auto pglobal = local2global (b, plocal);
  return rm_attr (pglobal, attr, fg, bg);
}
int rm_attr
(Box b, Cell plocal, tb_attr attr) {
  const auto pglobal = local2global (b, plocal);
  return rm_attr (pglobal, attr, true, true);
}
int rm_attr_fg
(Box b, Cell plocal, tb_attr attr) {
  const auto pglobal = local2global (b, plocal);
  return rm_attr (pglobal, attr, true, false);
}
int rm_attr_bg
(Box b, Cell plocal, tb_attr attr) {
  const auto pglobal = local2global (b, plocal);
  return rm_attr (pglobal, attr, false, true);
}


bool check_attr
(Cell p, tb_attr attr, bool fg, bool bg) {
  tb_cell *c;
  const auto rc = tb_get_cell(p.j, p.i, 1, &c);  // get from back buffer
  if (rc != TB_OK) {
    return rc;
  }
  bool has_fg = fg ? (c->fg & attr) : true;
  bool has_bg = bg ? (c->bg & attr) : true;
  return has_fg & has_bg;
};
bool check_attr
(Box b, Cell plocal, tb_attr attr, bool fg, bool bg) {
  const auto pglobal = local2global (b, plocal);
  return check_attr (pglobal, attr, fg, bg);
}


// returns nchars written
int write_string
(Cell start, size_t nchar, std::string_view s, tb_attr fg, tb_attr bg) {
  // TODO bounds checking
  assert (start.j >= 0);
  const size_t lim = (nchar > 0) ? std::min({nchar, s.size()}) : s.size();
  int nout = 0;
  for (size_t j = 0; j < lim; ++j) {
    const auto rc = set_cell (
      {start.i, static_cast<int> (start.j + j)},
      s[j],
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
(Cell start, size_t nchar, std::string_view s, tb_attr attr) {
  return write_string (start, nchar, s, attr, attr);
}
int write_string
(Cell start, size_t nchar, std::string_view s) {
  return write_string (start, nchar, s, 0, 0);
}
int write_string
(Box b, Cell local_start, size_t nchar, std::string_view s, tb_attr fg, tb_attr bg) {
  assert (local_within (b, local_start));
  const size_t j_avail = b.jglobal().last - local_start.j;
  const size_t req_chars = (nchar > 0) ? std::min({nchar, s.size()}) : s.size();
  const auto char_lim = std::min({j_avail, req_chars});
  const auto global_start = local2global (b, local_start);
  return write_string(global_start, char_lim, s, fg, bg);
}
int write_string
(Box b, Cell local_start, size_t nchar, std::string_view s, tb_attr attr) {
  return write_string (b, local_start, nchar, s, attr, attr);
}
int write_string
(Box b, Cell local_start, size_t nchar, std::string_view s) {
  return write_string (b, local_start, nchar, s, 0, 0);
}
int write_string
(Box b, Cell local_start, std::string_view s, tb_attr fg, tb_attr bg) {
  return write_string (b, local_start, 0, s, fg, bg);
}
int write_string
(Box b, Cell local_start, std::string_view s, tb_attr attr) {
  return write_string (b, local_start, 0, s, attr);
}
int write_string
(Box b, Cell local_start, std::string_view s) {
  return write_string (b, local_start, 0, s);
}


Cell local2global
(Box b, Cell c) {
  // TODO bounds checking
  return { c.i + b.iglobal().first, c.j + b.jglobal().first };
}
Cell global2local
(Box b, Cell c) {
  Cell clocal;
  const auto bi = b.iglobal();
  if (bi.contains(c.i)) {
    clocal.i = -1;
  } else {
    clocal.i = c.i - bi.first;
  }
  const auto bj = b.jglobal();
  if (bj.contains(c.j)) {
    clocal.j = -1;
  } else {
    clocal.j = c.j - bj.first;
  }
  return clocal;
}
Span local2global
(Box b, Span s, Axis ax) {
  const auto bspan = (ax == Axis::i) ? b.iglobal() : b.jglobal();
  return s + bspan;
}
Span global2local
(Box b, Span s, Axis ax) {
  const auto bspan = (ax == Axis::i) ? b.iglobal() : b.jglobal();
  return s - bspan;
}

bool global_within
(Box b, Cell c) {
  const auto plocal = global2local (b, c);
  return (c.valid() && local_within (b, plocal));
}
bool local_within
(Box b, Cell c) {
  return (c.i <= b.ilocal().last && c.j <= b.jlocal().last);
}


int clear
(Cell c) {
  return set_cell (c, ' ');
}
int clear
(Box b, Cell clocal) {
  return set_cell (b, clocal, ' ');
}
int clear
(Box b) {
  for (int i = 0; i <= b.ilocal().last; ++i) {
    for (int j = 0; j <= b.jlocal().last; ++j) {
      const auto rc = set_cell (b, {i, j}, ' ');
      if (rc != TB_OK) {
        return rc;
      };
    }
  }
  return TB_OK;
}

}  // end namespace
