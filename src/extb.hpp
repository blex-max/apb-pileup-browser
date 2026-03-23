#pragma once

#include <string_view>

// NOTE: on API design
// free functions favoured where a concept applies over more than one type
// and to minimise hassle with access to private data.
// Data types fixed on creation because they're small and I think
// it pays in terms of ease of reasoning to simply create a new one
// when one is needed, rather than modifying the existing one

// TODO:
// valid should probably be a free function for consistency

// NOTE:
// might eventually make this a C lib,
// when I can be bothered to deal with c strings

namespace extb {

// class extb_err : public std::runtime_error {
// public:
//     explicit extb_err (const std::string& msg)
//         : std::runtime_error (msg) {}
// };
// template<typename... Args>
// constexpr extb_err make_extb_err (std::format_string<Args...> fmt, Args&&... args) {
//     return extb_err (std::format(fmt, std::forward<Args> (args)...));
// }

// enum class err_code : int {
//     ok,
//     oob,
//     invalid_span
// };
// struct Err {
//     int tb2_err=TB_OK;
//     err_code extb_err=err_code::ok;
//     bool ok () const noexcept {
//         return tb2_err == TB_OK && extb_err == err_code::ok;
//     }
// };


enum class Axis {
    none,
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

// NOTE:
// Box intentionally has restricted constructors and accessors, with private internal coordinates
// A relatively limited set of approaches to interacting with a Box encourages consistency,
// and makes it harder to mess up the coordinate space.
// TODO last_local_* should be member functions
struct Box {
    private:
    // closed global coordinates
    int gi1=-1, gi2=-1, gj1=-1, gj2=-1;
    public:
    Box () = default;  // default-construct invalid

    bool valid () const noexcept {
        return Span{gi1, gi2}.valid() && Span{gj1, gj2}.valid();
    }

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

    // Box (int i1, int i2, int j1, int j2)
    // : gi1 (i1),
    //   gi2 (i2),
    //   gj1 (j1),
    //   gj2 (j2)
    //   {};
    Box (Span i, Span j)
    : gi1 (i.first),
      gi2 (i.last),
      gj1 (j.first),
      gj2 (j.last)
      {};
    Box (int i, Span j)
    : gi1 (i),
      gi2 (i),
      gj1 (j.first),
      gj2 (j.last)
      {};  // row shorthand
    Box (Span i, int j)
    : gi1 (i.first),
      gi2 (i.last),
      gj1 (j),
      gj2 (j)
      {};  // col shorthand
};
int last_local_i (Box b) noexcept;
int last_local_j (Box b) noexcept;

// NOTE handle grouped disjoint cells
// for writing, dimming, setting attrs, etc
// using CellGroup = std::vector<Cell>;

using tb_attr = unsigned short;
struct Style {
    private:
    tb_attr fg_attr;
    tb_attr bg_attr;

    public:
    tb_attr fg () const noexcept { return fg_attr; }
    tb_attr bg () const noexcept { return bg_attr; }

    Style (tb_attr fg, tb_attr bg) : fg_attr(fg), bg_attr(bg) {}
    Style (tb_attr attr) : fg_attr(attr), bg_attr(attr) {}
    Style () = delete;
};

Cell translate_cell (Box b, Cell local);
Span translate_span (Box b, Span local);


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
int set_attr
(Box b, Style style);  // all

int add_attr
(Cell cglobal, Style style);
int add_attr
(Box b, Cell clocal, Style style);
int add_attr
(Box b, Style style);  // all

int rm_attr
(Cell cglobal, Style style);
int rm_attr
(Box b, Cell clocal, Style style);
int rm_attr
(Box b, Style style);  // all

bool check_attr
(Cell cglobal, Style style);
bool check_attr
(Box b, Cell clocal, Style style);

size_t write_string
(Cell start, size_t nchar, std::string_view s, Style style={0});
size_t write_string
(Box b, Cell local_start, size_t nchar, std::string_view s, Style style = {0});
size_t write_string
(Box b, Cell local_start, std::string_view s, Style style = {0});

} // end namespace
