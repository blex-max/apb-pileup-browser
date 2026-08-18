#pragma once

#include <cstdint>
extern "C" {
#include "termbox2.h"
}

#include <ranges>
#include <span>
#include <string_view>

// NOTE: on API design
// - free functions favoured where a concept applies over more than one type
// and to minimise hassle with type design.
// - A relatively limited set of approaches to interacting with a the screen
// encourages consistency, and makes it harder to mess up the coordinate space.
// - A UI lib should model an area on a surface and no more,
// else it becomes difficult to integrate.
// - Single global coordinate space for drawing functions; reduces error space.

// NOTE:
// All coordinates are 0-based and use ij notation.

// NOTE:
// range coordinates (Span bounds, and any
// bound derived from them, e.g. write_string's
// j_bound) are 0-based END-EXCLUSIVE: `last` is
// one past the last valid index. Point coordinates
// (Cell/GlobalCell/LocalCell) are unaffected.

// TODO:
// - support extended grapheme clustering (helper fn for picking tb_set fns, span overload for extb::set)
// - bump style attr type to 64 bit to support TB_OPT_ATTR_W=64

namespace extb {

// --- TYPES & DECLARATIONS --- //

// A single character cell on the screen
struct Cell {
  int i = -1, j = -1;
};
struct GlobalCell : public Cell {};  // for global drawing fns
struct LocalCell : public Cell {
};  // For operations local to a shape abstraction (not used in this header)

bool valid (const Cell& c) noexcept;

// translating cells
struct Delta {
  int di = 0, dj = 0;
};
Delta dI (int n) noexcept;
Delta dJ (int n) noexcept;
Delta dIJ (int nI, int nJ) noexcept;

template <typename C>
concept CellType = std::derived_from<C, Cell>;

template <CellType C>
C operator+ (C c, const Delta& d) noexcept;
template <CellType C>
C operator- (C c, const Delta& d) noexcept;

// template <CellType C>
// void operator+= (C& c, const Delta& d) noexcept;

// all drawing functions may operate on a
// single global cell, a range of global cells,
// or an object for which a function
// `cell_source` can be found
// (which converts that object to
// a range of global cells).
// See box.hpp for an example of
// `cell_source` implementation.
template <typename R>
concept GlobalCellRange =
    std::ranges::input_range<R> &&
    std::convertible_to<
        std::ranges::range_value_t<R>, GlobalCell>;

template <typename T>
concept ConvertsToGlobalCellRange = requires (T&& t) {
  { cell_source (std::forward<T> (t)) } -> GlobalCellRange;
};

template <typename T>
concept GlobalCellSource =
    std::same_as<std::remove_cvref_t<T>, GlobalCell> ||
    GlobalCellRange<T> || ConvertsToGlobalCellRange<T>;

// for styling cells
struct Style {
  uintattr_t fg;
  uintattr_t bg;

  Style (uintattr_t fg, uintattr_t bg) : fg (fg), bg (bg) {}
  Style (uintattr_t attr) : fg (attr), bg (attr) {}
  Style() = delete;
};

// NOTE: std::expected return for global drawing fns?

// Draw a character to cell/s.
template <GlobalCellSource S>
int set (S&& gcs, uint32_t ch, const Style& style = {0});
#ifdef TB_OPT_EGC
// overload for EGC
template <GlobalCellSource S>
int set (S&& gcs, std::span<uint32_t> ech, const Style& = {0});

// add grapheme to cell
template <GlobalCellSource S>
int extend (S&& gcs, uint32_t ex);
#endif

// set cell/s to an empty space
// with no styling, i.e. blank.
// A shorthand for set (src, ' ').
template <GlobalCellSource S>
int clear (S&& gcs);

// set the style of cell/s,
// clearing previous styling
template <GlobalCellSource S>
int set_attr (S&& gcs, const Style& style);

// add style attributes to cell/s,
// without clearing existing
// styling.
template <GlobalCellSource S>
int add_attr (S&& gcs, const Style& style);

// remove style attributes from cell/s,
// retaining other styling.
template <GlobalCellSource S>
int rm_attr (S&& gcs, const Style& style);

// remove all style attributes from cell/s.
template <GlobalCellSource S>
int clear_attrs (S&& gcs);

// check presence of style attributes
// in termbox2 back buffer in all cell/s.
template <GlobalCellSource S>
bool check_attr_all_back (S&& gcs, const Style& style);

// TODO:
// template <GlobalCellSource S>
// bool check_attr_all_front
// (S&& gcs, const Style& style);

// template <GlobalCellSource S>
// bool check_attr_any_back
// (S&& gcs, const Style& style);
// template <GlobalCellSource S>
// bool check_attr_any_front
// (S&& gcs, const Style& style);

// write ascii string to display.
// NOTE: may expand to cover
// UTF-8, etc., in future.
// (and by extension extb::set_ex)
size_t write_ascii_string (
    GlobalCell start, int j_bound, std::string_view s,
    const Style& style = {0}
);

// --- END TYPES & DECLARATIONS --- //

// --- INTERNALS --- //

inline auto as_global_cell_range (GlobalCell cell)
{
  return std::views::single (cell);
}

template <GlobalCellRange R>
inline decltype (auto) as_global_cell_range (R&& r)
{
  return std::forward<R> (r);
}

template <ConvertsToGlobalCellRange T>
inline decltype (auto) as_global_cell_range (T&& t)
{
  return cell_source (std::forward<T> (t));
}

// --- END INTERNALS --- //

// --- IMPLEMENTATION --- //


namespace internal {

inline int mod_attr_egc (
    int x, int y, const tb_cell* tbc, uintattr_t fg,
    uintattr_t bg
)
{
  int rc = TB_OK;
#ifdef TB_OPT_EGC
  if (tbc->nech > 0) {
    rc = tb_set_cell_ex (x, y, tbc->ech, tbc->nech, fg, bg);
  }
  else {
    rc = tb_set_cell (x, y, tbc->ch, fg, bg);
  }
#else
  rc = tb_set_cell (x, y, tbc->ch, fg, bg);
#endif

  return rc;
}

}  // namespace internal


inline Delta dI (int n) noexcept { return {n, 0}; }
inline Delta dJ (int n) noexcept { return {0, n}; }
inline Delta dIJ (int nI, int nJ) noexcept { return {nI, nJ}; };

template <CellType C>
C operator+ (C c, const Delta& d) noexcept
{
  c.i += d.di;
  c.j += d.dj;
  return c;
}

template <CellType C>
C operator- (C c, const Delta& d) noexcept
{
  c.i -= d.di;
  c.j -= d.dj;
  return c;
}

// template <CellType C>
// void operator+= (C& c, const Delta& d) noexcept
// {
//   c.i += d.di;
//   c.j += d.dj;
// }

template <GlobalCellSource S>
int set (S&& gcs, uint32_t ch, const Style& style)
{
  for (const auto gc : as_global_cell_range (gcs)) {
    const auto rc =
        tb_set_cell (gc.j, gc.i, ch, style.fg, style.bg);
    if (rc != TB_OK) {
      return rc;
    };
  }
  return TB_OK;
}

#ifdef TB_OPT_EGC
template <GlobalCellSource S>
int set (S&& gcs, std::span<uint32_t> ech, const Style& style)
{
  for (const auto gc : as_global_cell_range (gcs)) {
    const auto rc = tb_set_cell_ex (
        gc.j, gc.i, ech.data(), ech.size(), style.fg, style.bg
    );
    if (rc != TB_OK) {
      return rc;
    };
  }
  return TB_OK;
}

template <GlobalCellSource S>
int extend (S&& gcs, uint32_t ex)
{
  for (const auto gc : as_global_cell_range (gcs)) {
    const auto rc = tb_extend_cell (gc.j, gc.i, ex);
    if (rc != TB_OK) {
      return rc;
    };
  }
  return TB_OK;
}
#endif

template <GlobalCellSource S>
int clear (S&& gcs)
{
  return set (gcs, ' ');
}

template <GlobalCellSource S>
int set_attr (S&& gcs, const Style& style)
{
  tb_cell* tbc;
  int rc;
  for (const auto& gc : as_global_cell_range (gcs)) {
    // get from back buffer
    rc = tb_get_cell (gc.j, gc.i, 1, &tbc);
    if (rc != TB_OK) {
      return rc;
    }
    const uintattr_t fg_attr = style.fg ? style.fg : tbc->fg;
    const uintattr_t bg_attr = style.bg ? style.bg : tbc->bg;
    rc = tb_set_cell (gc.j, gc.i, tbc->ch, fg_attr, bg_attr);
    if (rc != TB_OK) {
      return rc;
    }
  }
  return TB_OK;
}

template <GlobalCellSource S>
int add_attr (S&& gcs, const Style& style)
{
  tb_cell* tbc;
  int rc;
  for (const auto& gc : as_global_cell_range (gcs)) {
    rc = tb_get_cell (gc.j, gc.i, 1, &tbc);
    if (rc != TB_OK) {
      return rc;
    }
    const uintattr_t fg_attr = tbc->fg | style.fg;
    const uintattr_t bg_attr = tbc->bg | style.bg;
    rc = internal::mod_attr_egc (
        gc.j, gc.i, tbc, fg_attr, bg_attr
    );
    if (rc != TB_OK) {
      return rc;
    }
  }
  return TB_OK;
}

template <GlobalCellSource S>
int rm_attr (S&& gcs, const Style& style)
{
  tb_cell* tbc;
  int rc;
  for (const auto& gc : as_global_cell_range (gcs)) {
    rc = tb_get_cell (gc.j, gc.i, 1, &tbc);
    if (rc != TB_OK) {
      return rc;
    }
    const uintattr_t fg_attr = tbc->fg & ~style.fg;
    const uintattr_t bg_attr = tbc->bg & ~style.bg;
    rc = internal::mod_attr_egc (
        gc.j, gc.i, tbc, fg_attr, bg_attr
    );
    if (rc != TB_OK) {
      return rc;
    }
  }
  return TB_OK;
}

template <GlobalCellSource S>
int clear_attrs (S&& gcs)
{
  tb_cell* tbc;
  int rc;
  for (const auto& gc : as_global_cell_range (gcs)) {
    rc = tb_get_cell (gc.j, gc.i, 1, &tbc);
    if (rc != TB_OK) {
      return rc;
    }
    rc = internal::mod_attr_egc (gc.j, gc.i, tbc, 0, 0);
    if (rc != TB_OK) {
      return rc;
    }
  }
  return TB_OK;
}

template <GlobalCellSource S>
int check_attr_all_back (S&& gcs, const Style& style)
{
  tb_cell* tbc;
  int rc;
  for (const auto& gc : as_global_cell_range (gcs)) {
    rc = tb_get_cell (gc.j, gc.i, 1, &tbc);
    if (rc != TB_OK) {
      return false;  // misleading?
    }
    const bool has_fg = style.fg ? (tbc->fg & style.fg) : true;
    const bool has_bg = style.bg ? (tbc->bg & style.bg) : true;
    if (!has_fg || !has_bg) {
      return false;
    }
  }
  return true;
}

inline bool valid (const Cell& c) noexcept
{
  return (c.i >= 0 && c.j >= 0);
}

// returns nchars written
inline size_t write_ascii_string (
    GlobalCell start, int j_bound, std::string_view s,
    const Style& style
)
{
  if (!valid (start) || s.empty() || j_bound < 0) {
    return 0;
  }

  const auto jlim = std::min<size_t> (
      s.size(), static_cast<size_t> (j_bound - start.j)
  );

  int nout = 0;
  for (size_t j = 0; j < jlim; ++j) {
    const auto rc =
        set (start, static_cast<unsigned char> (s[j]), style);
    if (rc != TB_OK) {
      break;
    }
    ++start.j;
    ++nout;
  }
  return static_cast<size_t> (nout);
}

// --- END IMPLEMENTATION --- //

}  // end namespace extb
