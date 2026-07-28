#pragma once

extern "C" {
#include "termbox2.h"
}

#include <ranges>
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
Delta I (int n) noexcept;
Delta J (int n) noexcept;

template <typename C>
concept CellType = std::derived_from<C, Cell>;

template <CellType C>
C translate (C c, Delta d) noexcept;

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
using tb_attr = unsigned short;
struct Style {
  tb_attr fg;
  tb_attr bg;

  Style (tb_attr fg, tb_attr bg) : fg (fg), bg (bg) {}
  Style (tb_attr attr) : fg (attr), bg (attr) {}
  Style() = delete;
};

// NOTE: std::expected return for global drawing fns?

// Draw a character to cell/s.
template <GlobalCellSource S>
int set (S&& gcs, uint32_t ch, const Style& style = {0});

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

// write string to display.
size_t write_string (
    const GlobalCell& start, int j_bound, std::string_view s,
    const Style& style = {0}
);

// --- END TYPES & DECLARATIONS --- //

// --- INTERNALS --- //

static auto as_global_cell_range (GlobalCell cell)
{
  return std::views::single (cell);
}

template <GlobalCellRange R>
static decltype (auto) as_global_cell_range (R&& r)
{
  return std::forward<R> (r);
}

template <ConvertsToGlobalCellRange T>
static decltype (auto) as_global_cell_range (T&& t)
{
  return cell_source (std::forward<T> (t));
}

// --- END INTERNALS --- //

// --- IMPLEMENTATION --- //

inline Delta I (int n) noexcept { return {n, 0}; }
inline Delta J (int n) noexcept { return {0, n}; }

template <CellType C>
C translate (C c, Delta d) noexcept
{
  c.i += d.di;
  c.j += d.dj;
  return c;
}

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
    const auto fg_attr = style.fg ? style.fg : tbc->fg;
    const auto bg_attr = style.bg ? style.bg : tbc->bg;
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
    const tb_attr fg_attr = tbc->fg | style.fg;
    const tb_attr bg_attr = tbc->bg | style.bg;
    rc = tb_set_cell (gc.j, gc.i, tbc->ch, fg_attr, bg_attr);
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
    const tb_attr fg_attr = tbc->fg & ~style.fg;
    const tb_attr bg_attr = tbc->bg & ~style.bg;
    rc = tb_set_cell (gc.j, gc.i, tbc->ch, fg_attr, bg_attr);
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
    rc = tb_set_cell (gc.j, gc.i, tbc->ch, 0, 0);
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
inline size_t write_string (
    const GlobalCell& start, int j_bound, std::string_view s,
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
    const auto rc = set (
        translate (start, J (static_cast<int> (j))),
        static_cast<unsigned char> (s[j]), style
    );
    if (rc != TB_OK) {
      break;
    }
    ++nout;
  }
  return static_cast<size_t> (nout);
}

// --- END IMPLEMENTATION --- //

}  // end namespace extb
