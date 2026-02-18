#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <string_view>

extern "C" {
    #include "termbox2.h"
}

constexpr int EXTB_ERR_OOB = -50;

namespace extb {

enum class Axis : size_t {
    i,
    j
};

struct Cell {
    int
    i=-1,
    j=-1;

    bool valid () const noexcept {
        return (i >= 0 && j >= 0);
    }
};

struct Span {
    // inclusive coordinates
    const int
    first=-1,
    last=-1;

    bool valid () const noexcept {
        return (first >= 0 && last >= 0 && last >= first);
    }

    size_t size () const noexcept {
        return valid() ? last - first + 1 : 0;
    }

    bool contains (int p) const noexcept {
        return valid() && (p >= first && p <= last);
    }
    bool contains (Span s) const noexcept {
        return valid() && (first <= s.first && last >= s.last);
    }

    Span operator+(const Span& other) const noexcept {
        return { first + other.first, last + other.last };
    }
    Span operator-(const Span& other) const noexcept {
        return { first - other.first, last - other.last };
    }
};

struct Box {
    private:
    // closed global coordinates
    int gi1=-1, gi2=-1, gj1=-1, gj2=-1;
    explicit Box (int global_i1, int global_i2, int global_j1, int global_j2)
    : gi1 (global_i1),
      gi2 (global_i2),
      gj1 (global_j1),
      gj2 (global_j2)
      {};

    public:
    Box () = default;  // default-construct invalid
    // bool valid ();

    Span i () const noexcept {
        return {gi1, gi2};
    }
    Span j () const noexcept {
        return {gj1, gj2};
    }

    bool contains (Cell cglobal) const noexcept {
        return cglobal.i >= gi1 && cglobal.i <= gi2 && cglobal.j >= gj1 && cglobal.j <= gj2;
    }

    friend int last_local_i (Box b) noexcept;
    friend int last_local_j (Box b) noexcept;

    friend Box make_box (Span i, Span j);
    friend Box make_row (int i, Span j);
    friend Box make_col (int j, Span i);
};
int last_local_i (Box b) noexcept;
int last_local_j (Box b) noexcept;
Box make_box (Span i, Span j);
Box make_row (int i, Span j);
Box make_col (int j, Span i);

// NOTE handle grouped disjoint cells
// for writing, dimming, setting attrs, etc
// using CellGroup = std::vector<Cell>;

using tb_attr = unsigned short;
struct Style {
    private:
    std::optional<tb_attr> fg_attr;
    std::optional<tb_attr> bg_attr;

    public:
    std::optional<tb_attr> fg () const noexcept { return fg_attr; }
    std::optional<tb_attr> bg () const noexcept { return bg_attr; }

    Style (std::optional<tb_attr> fg, std::optional<tb_attr> bg) : fg_attr(fg), bg_attr(bg) {}
    Style (tb_attr attr) : fg_attr(attr), bg_attr(attr) {}
    Style () = delete;
};

Cell translate_cell (Box b, Cell local);
Span translate_span (Box b, Span local);

int set_cell
(Cell cglobal, uint32_t ch, Style style={0});
int set_cell
(Box b, Cell clocal, uint32_t ch, Style={0});
int set_cell
(Box b, uint32_t ch, Style style={0});  // set all

int clear
(Cell cglobal);
int clear
(Box b, Cell clocal);
int clear
(Box b);  // clear all

int set_attr
(Cell cglobal, Style style);
int set_attr
(Box b, Cell clocal, Style style);

int add_attr
(Cell cglobal, Style style);
int add_attr
(Box b, Cell clocal, Style style);

int rm_attr
(Cell cglobal, Style style);
int rm_attr
(Box b, Cell clocal, Style style);

bool check_attr
(Cell cglobal, Style style);
bool check_attr
(Box b, Cell clocal, Style style);

int write_string
(Cell start, size_t nchar, std::string_view s, Style style={0});
int write_string
(Box b, Cell local_start, size_t nchar, std::string_view s, Style style = {0});
int write_string
(Box b, Cell local_start, std::string_view s, Style style = {0});
}
