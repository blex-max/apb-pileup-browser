#pragma once

#include <span>
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
// coordinates are 0-based END-INCLUSIVE.

// NOTE:
// might eventually make this a C lib,
// when I can be bothered to deal with c strings

namespace extb {

struct Cell {
    int
    i=-1,
    j=-1;
};
struct GlobalCell : public Cell {};
struct LocalCell : public Cell {};


struct Span {
    int
    first=-1,
    last=-1;

    // member function as a concession to
    // cpp as it looks markedly odd in use
    // as a free function.
    size_t size () const noexcept;
};
struct GlobalSpan : public Span {};
struct LocalSpan : public Span {};
bool contains (const Span& s, int p) noexcept;
bool contains (const GlobalSpan& outer, const GlobalSpan& inner) noexcept;
bool contains (const LocalSpan& outer, const LocalSpan& inner) noexcept;

// closed/inclusive global coordinates
struct GlobalBox {
    // default-construct invalid
    GlobalSpan ispan;
    GlobalSpan jspan;
};
// box factories
GlobalBox make_box (const GlobalSpan& ispan, const GlobalSpan& jspan);
GlobalBox make_row (int i, const GlobalSpan& jspan);
GlobalBox make_col (const GlobalSpan& ispan, int j);
// box transforms

// functions relevant or shared across types
int last_local (const Span& s) noexcept;
LocalCell last_local (const GlobalBox& b) noexcept;
bool valid (const Cell& c) noexcept;
bool valid (const Span& s) noexcept;
bool valid (const GlobalBox& b) noexcept;

GlobalCell to_global (const GlobalBox& b, const LocalCell& local);
bool contains_global (const GlobalBox& b, const GlobalCell& cglobal) noexcept;
bool contains_local (const GlobalBox& b, const LocalCell& clocal) noexcept;


// NOTE: handle grouped disjoint cells
// for writing, dimming, setting attrs, etc
// using CellGroup = std::span<Cell>;
// CellGroup to_cells (const GlobalBox& b)
// then most functions could just take a span of cells?
// It would be nice to write a way to get an iterator
// or similar from GlobalBox so those cells don't
// actually have to be unnecessarily materialised.
// TODO!


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

int set
(const GlobalCell& c, uint32_t ch, const Style& style={0});
int set_box
(const GlobalBox& b, uint32_t ch, const Style& style={0});  // set all

int clear
(const GlobalCell& c);
int clear_box
(const GlobalBox& b);  // clear all

int set_attr
(const GlobalCell& c, const Style& style);
int set_attr_box
(const GlobalBox& b, const Style& style);  // all

int add_attr
(const GlobalCell& c, const Style& style);
int add_attr_box
(const GlobalBox& b, const Style& style);  // all

int rm_attr
(const GlobalCell& c, const Style& style);
int rm_attr_box
(const GlobalBox& b, const Style& style);  // all

bool check_attr
(const GlobalCell& c, const Style& style);

size_t write_string
(const GlobalCell& start, std::string_view s, const Style& style={0});
size_t write_string_within  // bounded by box
(const GlobalCell& start, const GlobalBox& b, std::string_view s, const Style& style={0});

} // end namespace
