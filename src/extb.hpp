#pragma once

#include <cassert>
#include <cstdint>
#include <string_view>

extern "C" {
    #include "termbox2.h"
}

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
    Box (int i1, int i2, int j1, int j2)
    : gi1 (i1),
      gi2 (i2),
      gj1 (j1),
      gj2 (j2)
      {};

    public:
    explicit Box () = default;  // default-construct invalid
    // bool valid ();

    Span iglobal () const noexcept {
        return {gi1, gi2};
    }
    Span jglobal () const noexcept {
        return {gj1, gj2};
    }
    Span ilocal () const noexcept {
        const auto ilast = gi2 - gi1;
        return (ilast < 0) ? Span{-1, -1} : Span{0, ilast};
    }
    Span jlocal () const noexcept {
        const auto jlast = gj2 - gj1;
        return (jlast < 0) ? Span{-1, -1} : Span{0, jlast};
    }
    // int ilast () const noexcept {
    //     const auto ilast = gi2 - gi1;
    //     return (ilast < 0) ? -1 : ilast;
    // }
    // int jlast () const noexcept {
    //     const auto ilast = gi2 - gi1;
    //     return (ilast < 0) ? -1 : ilast;
    // }

    static Box make_box (Span i, Span j);
    static Box make_row (int i, Span j);
    static Box make_col (int j, Span i);
};


// TODO handle grouped disjoint cells
// for writing, dimming, setting attrs, etc
// using CellGroup = std::vector<Cell>;


using tb_attr = unsigned short;

int set_cell
(Cell p, uint32_t ch, tb_attr fg, tb_attr bg);
int set_cell
(Cell p, uint32_t ch, tb_attr attr);
int set_cell
(Cell p, uint32_t ch);
int set_cell
(Box b, Cell plocal, uint32_t ch, tb_attr fg, tb_attr bg);
int set_cell
(Box b, Cell plocal, uint32_t ch, tb_attr attr);
int set_cell
(Box b, Cell plocal, uint32_t ch);
int set_cell
(Box b, uint32_t ch, tb_attr fg, tb_attr bg);  // set all
int set_cell
(Box b, uint32_t ch, tb_attr attr);
int set_cell
(Box b, uint32_t ch);


int set_attr
(Cell p, tb_attr attr, bool fg, bool bg);
int set_attr
(Cell p, tb_attr attr);
int set_attr_fg
(Cell p, tb_attr attr);
int set_attr_bg
(Cell p, tb_attr attr);
int set_attr
(Box b, Cell plocal, tb_attr attr, bool fg, bool bg);
int set_attr
(Box b, Cell plocal, tb_attr attr);
int set_attr_fg
(Box b, Cell plocal, tb_attr attr);
int set_attr_bg
(Box b, Cell plocal, tb_attr attr);

int add_attr
(Cell p, tb_attr attr, bool fg, bool bg);
int add_attr
(Cell p, tb_attr attr);
int add_attr_fg
(Cell p, tb_attr attr);
int add_attr_bg
(Cell p, tb_attr attr);
int add_attr
(Box b, Cell plocal, tb_attr attr, bool fg, bool bg);
int add_attr
(Box b, Cell plocal, tb_attr attr);
int add_attr_fg
(Box b, Cell plocal, tb_attr attr);
int add_attr_bg
(Box b, Cell plocal, tb_attr attr);

int rm_attr
(Cell p, tb_attr attr, bool fg, bool bg);
int rm_attr
(Cell p, tb_attr attr);
int rm_attr_fg
(Cell p, tb_attr attr);
int rm_attr_bg
(Cell p, tb_attr attr);
int rm_attr
(Box b, Cell plocal, tb_attr attr, bool fg, bool bg);
int rm_attr
(Box b, Cell plocal, tb_attr attr);
int rm_attr_fg
(Box b, Cell plocal, tb_attr attr);
int rm_attr_bg
(Box b, Cell plocal, tb_attr attr);

bool check_attr
(Cell p, tb_attr attr, bool fg, bool bg);
bool check_attr
(Box b, Cell plocal, tb_attr attr, bool fg, bool bg);

int write_string
(Cell start, size_t nchar, std::string_view s, tb_attr fg, tb_attr bg);
int write_string
(Cell start, size_t nchar, std::string_view s, tb_attr attr);
int write_string
(Cell start, size_t nchar, std::string_view s);
int write_string
(Box b, Cell local_start, size_t nchar, std::string_view s, tb_attr fg, tb_attr bg);
int write_string
(Box b, Cell local_start, size_t nchar, std::string_view s, tb_attr attr);
int write_string
(Box b, Cell local_start, size_t nchar, std::string_view s);
int write_string
(Box b, Cell local_start, std::string_view s, tb_attr fg, tb_attr bg);
int write_string
(Box b, Cell local_start, std::string_view s, tb_attr attr);
int write_string
(Box b, Cell local_start, std::string_view s);

// box funcs
Cell global2local
(Box b, Cell c);
Cell local2global
(Box b, Cell c);

bool global_within
(Box b, Cell cglobal);
bool local_within
(Box b, Cell clocal);

int clear
(Cell c);
int clear
(Box b, Cell c);
int clear
(Box b);

// make within local coordinates of other Box
Box make_sub_box (Box b, Span i, Span j);
Box make_sub_row (Box b, int i, Span j);
Box make_sub_row (Box b, int i);
Box make_sub_col (Box b, int j, Span i);
Box make_sub_col (Box b, int j);

// span funcs
Span local2global
(Box b, Span s, Axis ax);
Span global2local
(Box b, Span s, Axis ax);
}
