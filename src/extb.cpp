#include <cassert>
#include <cstdint>

#include "extb.hpp"
#include "util.hpp"

namespace extb {

int last_local_i (Box b) noexcept {
  return b.gi2 - b.gi1;
}
int last_local_j (Box b) noexcept {
  return b.gj2 - b.gj1;
}

Box make_box(Span i, Span j) {
  assert (i.valid());
  assert (j.valid());
  return Box{i.first, i.last, j.first, j.last};
}
Box make_row (int i, Span j) {
  return make_box ({i, i}, j);
}
Box make_col (int j, Span i) {
  return make_box (i, {j, j});
}


Cell translate_cell
(Box b, Cell local) {
  return { local.i + b.i().first, local.j + b.j().first};
}


int set_cell
(Cell c, uint32_t ch, Style style) {
  return tb_set_cell(c.j, c.i, ch, style.fg() ? *style.fg() : 0., style.bg() ? *style.bg() : 0);
};
int set_cell
(Box b, uint32_t ch, Style style) {
  for (int i = b.i().first; i <= b.i().last; ++i) {
    for (int j = b.j().first; j <= b.j().last; ++j) {
      const auto rc = set_cell ({i, j}, ch, style);
      if (rc != TB_OK) {
        return rc;
      };
    }
  }
  return TB_OK;
}


int clear
(Cell c) {
  return set_cell (c, ' ');
}
int clear
(Box b) {
  for (int i = b.i().first; i <= b.i().last; ++i) {
    for (int j = b.j().first; j <= b.j().last; ++j) {
      const auto rc = set_cell ({i, j}, ' ');
      if (rc != TB_OK) {
        return rc;
      };
    }
  }
  return TB_OK;
}


int set_attr
(Cell p, Style style) {
  tb_cell *c;
  // get from back buffer
  if (const auto rc = tb_get_cell(p.j, p.i, 1, &c); rc != TB_OK) {
    return rc;
  }
  const auto fg_attr = style.fg() ? *style.fg() : c->fg;
  const auto bg_attr = style.bg() ? *style.bg() : c->bg;
  return tb_set_cell(p.j, p.i, c->ch, fg_attr, bg_attr);
};
// int set_attr
// (Box b, Cell plocal, tb_attr attr, bool fg, bool bg) {
//   const auto pglobal = local2global (b, plocal);
//   return set_attr (pglobal, attr, fg, bg);
// }



int add_attr
(Cell p, Style style) {
  tb_cell *c;
  if (const auto rc = tb_get_cell(p.j, p.i, 1, &c); rc != TB_OK) {
    return rc;
  }
  const tb_attr fg_attr = style.fg() ? (c->fg | *style.fg()) : c->fg;
  const tb_attr bg_attr = style.bg() ? (c->bg | *style.bg()) : c->bg;
  return tb_set_cell(p.j, p.i, c->ch, fg_attr, bg_attr);
};
int add_attr
(Box b, Cell clocal, Style style) {
  return add_attr (translate_cell(b, clocal), style);
}



int rm_attr
(Cell c, Style style) {
  tb_cell *cbuf;
  if (const auto rc = tb_get_cell(c.j, c.i, 1, &cbuf); rc != TB_OK) {
    return rc;
  }
  const tb_attr fg_attr = style.fg() ? (cbuf->fg & ~*style.fg()) : cbuf->fg;
  const tb_attr bg_attr = style.bg() ? (cbuf->bg & ~*style.bg()) : cbuf->bg;
  return tb_set_cell(c.j, c.i, cbuf->ch, fg_attr, bg_attr);
};
int rm_attr
(Box b, Cell clocal, Style style) {
  return rm_attr (translate_cell(b, clocal), style);
}


bool check_attr
(Cell p, Style style) {
  tb_cell *c;
  if (const auto rc = tb_get_cell(p.j, p.i, 1, &c); rc != TB_OK) {
    return rc;
  }
  bool has_fg = style.fg() ? (c->fg & *style.fg()) : true;
  bool has_bg = style.bg() ? (c->bg & *style.bg()) : true;
  return has_fg & has_bg;
};
// bool check_attr
// (Box b, Cell plocal, tb_attr attr, bool fg, bool bg) {
//   const auto pglobal = local2global (b, plocal);
//   return check_attr (pglobal, attr, fg, bg);
// }


// returns nchars written
int write_string
(Cell start, size_t nchar, std::string_view s, Style style) {
  // TODO bounds checking
  assert (start.j >= 0);
  const size_t lim = (nchar > 0) ? std::min({nchar, s.size()}) : s.size();
  int nout = 0;
  for (size_t j = 0; j < lim; ++j) {
    const auto rc = set_cell (
      {start.i, static_cast<int> (start.j + j)},
      s[j],
      style
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
(Box b, Cell local_start, size_t nchar, std::string_view s, Style style) {
  // log_err ("local cell: {}, {}", local_start.i, local_start.j);
  auto cglobal = translate_cell(b, local_start);
  // log_err ("global cell: {}, {}", cglobal.i, cglobal.j);
  if (!b.contains(cglobal)) {
    return EXTB_ERR_OOB;
  }
  const size_t j_avail = b.j().last - cglobal.j + 1;
  const size_t req_chars = (nchar > 0) ? std::min({nchar, s.size()}) : s.size();
  const auto char_lim = std::min({j_avail, req_chars});
  return write_string (cglobal, char_lim, s, style);
}
int write_string
(Box b, Cell local_start, std::string_view s, Style style) {
  return write_string (b, local_start, 0, s, style);
}


// Cell local2global
// (Box b, Cell c) {
//   // TODO bounds checking
//   return { c.i + b.iglobal().first, c.j + b.jglobal().first };
// }
// Cell global2local
// (Box b, Cell c) {
//   Cell clocal;
//   const auto bi = b.iglobal();
//   if (bi.contains(c.i)) {
//     clocal.i = -1;
//   } else {
//     clocal.i = c.i - bi.first;
//   }
//   const auto bj = b.jglobal();
//   if (bj.contains(c.j)) {
//     clocal.j = -1;
//   } else {
//     clocal.j = c.j - bj.first;
//   }
//   return clocal;
// }
// Span local2global
// (Box b, Span s, Axis ax) {
//   const auto bspan = (ax == Axis::i) ? b.iglobal() : b.jglobal();
//   return s + bspan;
// }
// Span global2local
// (Box b, Span s, Axis ax) {
//   const auto bspan = (ax == Axis::i) ? b.iglobal() : b.jglobal();
//   return s - bspan;
// }

// bool global_within
// (Box b, Cell c) {
//   const auto plocal = global2local (b, c);
//   return (c.valid() && local_within (b, plocal));
// }
// bool local_within
// (Box b, Cell c) {
//   return (c.i <= b.ilocal().last && c.j <= b.jlocal().last);
// }



}  // end namespace
