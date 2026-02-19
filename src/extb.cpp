#include <cassert>
#include <cstdint>

#include "extb.hpp"

namespace extb {

int last_local_i (Box b) noexcept {
  return b.gi2 - b.gi1;
}
int last_local_j (Box b) noexcept {
  return b.gj2 - b.gj1;
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
(Box b, Cell clocal, uint32_t ch, Style style) {
  auto cglobal = translate_cell(b, clocal);
  if (!b.contains(cglobal)) {
    return TB_ERR;
  }
  return set_cell (cglobal, ch, style);
}
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
  auto cglobal = translate_cell(b, clocal);
  if (!b.contains(cglobal)) {
    return TB_ERR;
  }
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
  auto cglobal = translate_cell(b, clocal);
  if (!b.contains(cglobal)) {
    return TB_ERR;
  }
  return rm_attr (cglobal , style);
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
size_t write_string
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
      break;
    }
    ++nout;
  }
  return nout;
}
size_t write_string
(Box b, Cell local_start, size_t nchar, std::string_view s, Style style) {
  // log_err ("local cell: {}, {}", local_start.i, local_start.j);
  auto cglobal = translate_cell(b, local_start);
  // log_err ("global cell: {}, {}", cglobal.i, cglobal.j);
  if (!b.contains(cglobal)) {
    return TB_ERR;
  }
  const size_t j_avail = b.j().last - cglobal.j + 1;
  const size_t req_chars = (nchar > 0) ? std::min({nchar, s.size()}) : s.size();
  const auto char_lim = std::min({j_avail, req_chars});
  return write_string (cglobal, char_lim, s, style);
}
size_t write_string
(Box b, Cell local_start, std::string_view s, Style style) {
  return write_string (b, local_start, 0, s, style);
}

}  // end namespace
