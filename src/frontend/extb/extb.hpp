#pragma once

extern "C" {
#include "termbox2.h"
}

#include <string_view>

// NOTE: on API design
// - free functions favoured where a concept applies over more than one type
// and to minimise hassle with access to private data.
// - A relatively limited set of approaches to interacting with a the screen
// encourages consistency, and makes it harder to mess up the coordinate space.
// - A UI lib should model an area on a surface and no more,
// else it becomes difficult to integrate.
// - Moving between coordinate spaces often leads to bugs;
// operations which affect display buffer only operate
// via global coordinates (GlobalCell or Box), and
// translation from local to global is provided.
// - Because there is no implicit translation of
// coordinate spaces within functions, the possible
// error space is greatly reduced

// NOTE:
// range coordinates (Span/Box bounds, and any
// bound derived from them, e.g. write_string's
// j_bound) are 0-based END-EXCLUSIVE: `last` is
// one past the last valid index. Point coordinates
// (Cell/GlobalCell/LocalCell) are unaffected, as a
// single cell has no "end".

namespace extb {

struct Cell {
  int i = -1, j = -1;
};
struct GlobalCell : public Cell {};
struct LocalCell : public Cell {
};  // For operations local to a shape abstraction

bool valid (const Cell& c) noexcept;

// all functions may operate on a
// single global cell, a range of global cells,
// or an object for which a function
// `to_global_cells` can be found
// which converts that object to
// a range of global cells
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

// TODO templatise and use ADL
// for any shape abstraction which has e.g.
// convert_local_cell defined
// GlobalCell to_global (const GlobalBox& b, const LocalCell& local) noexcept;
// bool contains_global (const GlobalBox& b, const GlobalCell& cglobal) noexcept;
// bool contains_local (const GlobalBox& b, const LocalCell& clocal) noexcept;

// for styling cells
using tb_attr = unsigned short;
struct Style {
 private:
  tb_attr fg_attr;
  tb_attr bg_attr;

 public:
  tb_attr fg() const noexcept { return fg_attr; }
  tb_attr bg() const noexcept { return bg_attr; }

  Style (tb_attr fg, tb_attr bg) : fg_attr (fg), bg_attr (bg) {}
  Style (tb_attr attr) : fg_attr (attr), bg_attr (attr) {}
  Style() = delete;
};

// NOTE: shared error space now
// less of an issue since mandatory
// translation to the global space
// minimises overlap
// TODO: error model for draw calls:
// take as optional output param
// a pointer (i.e. nullable) to a
// vector of DrawErr. Append only
// on failure
// struct DrawErr {
//     Cell c;
//     uint32_t ch;
//     int tb_err;
//     // extb_err
// };

template <GlobalCellSource S>
int set (S&& gcs, uint32_t ch, const Style& style = {0});

// set cells to an empty space
// with no styling, i.e. blank
template <GlobalCellSource S>
int clear (S&& gcs);

template <GlobalCellSource S>
int set_attr (S&& gcs, const Style& style);

template <GlobalCellSource S>
int add_attr (S&& gcs, const Style& style);

template <GlobalCellSource S>
int rm_attr (S&& gcs, const Style& style);

template <GlobalCellSource S>
int reset_attr (S&& gcs, const Style& style);

template <GlobalCellSource S>
bool check_attr_all_back (S&& gcs, const Style& style);
// template <GlobalCellSource S>
// bool check_attr_all_front
// (S&& gcs, const Style& style);

// template <GlobalCellSource S>
// bool check_attr_any_back
// (S&& gcs, const Style& style);
// template <GlobalCellSource S>
// bool check_attr_any_front
// (S&& gcs, const Style& style);

size_t write_string (
    const GlobalCell& start, int j_bound, std::string_view s,
    const Style& style = {0}
);

}  // end namespace extb

// template definitions
#include "extb.impl"
