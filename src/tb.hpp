#pragma once

#include <cassert>
#include <cstdint>
#include <string_view>

namespace extb {

struct Point {
    int
    x=-1,
    y=-1;

    bool valid () {
        return (x >= 0 && y >= 0);
    }
};
struct Box {
    private:
    Box (int x1, int x2, int y1, int y2)
    : gx1 (x1),
      gx2 (x2),
      gy1 (y1),
      gy2 (y2),
      xlast (x2 - x1),
      ylast (y2 - y1),
      xsz (xlast + 1),
      ysz (ylast + 1)
      {};

    public:
    // closed global coordinates
    const int gx1, gx2, gy1, gy2;
    const int xlast, ylast;
    const size_t xsz, ysz;

    static Box make_box (int x1, int x2, int y1, int y2) {
        assert (x2 >= x1);
        assert (y2 >= y1);
        Box b(x1, x2, y1, y2);
        return b;
    }
    static Box make_row (int x, int y1, int y2) {
        return make_box (x, x, y1, y2);
    }
    static Box make_col (int x1, int x2, int y) {
        return make_box (x1, x2, y, y);
    }
};


using tb_attr = unsigned short;

int set_cell
(Point p, uint32_t ch, tb_attr fg, tb_attr bg);
int set_cell
(Point p, uint32_t ch, tb_attr attr);
int set_cell
(Point p, uint32_t ch);
int set_cell
(Box b, Point plocal, uint32_t ch, tb_attr fg, tb_attr bg);
int set_cell
(Box b, Point plocal, uint32_t ch, tb_attr attr);
int set_cell
(Box b, Point plocal, uint32_t ch);
int set_cell
(Box b, uint32_t ch);  // set all


int set_attr
(Point p, tb_attr attr, bool fg, bool bg);
int set_attr
(Point p, tb_attr attr);
int set_attr_fg
(Point p, tb_attr attr);
int set_attr_bg
(Point p, tb_attr attr);
int set_attr
(Box b, Point plocal, tb_attr attr, bool fg, bool bg);
int set_attr
(Box b, Point plocal, tb_attr attr);
int set_attr_fg
(Box b, Point plocal, tb_attr attr);
int set_attr_bg
(Box b, Point plocal, tb_attr attr);

int add_attr
(Point p, tb_attr attr, bool fg, bool bg);
int add_attr
(Point p, tb_attr attr);
int add_attr_fg
(Point p, tb_attr attr);
int add_attr_bg
(Point p, tb_attr attr);
int add_attr
(Box b, Point plocal, tb_attr attr, bool fg, bool bg);
int add_attr
(Box b, Point plocal, tb_attr attr);
int add_attr_fg
(Box b, Point plocal, tb_attr attr);
int add_attr_bg
(Box b, Point plocal, tb_attr attr);

int rm_attr
(Point p, tb_attr attr, bool fg, bool bg);
int rm_attr
(Point p, tb_attr attr);
int rm_attr_fg
(Point p, tb_attr attr);
int rm_attr_bg
(Point p, tb_attr attr);
int rm_attr
(Box b, Point plocal, tb_attr attr, bool fg, bool bg);
int rm_attr
(Box b, Point plocal, tb_attr attr);
int rm_attr_fg
(Box b, Point plocal, tb_attr attr);
int rm_attr_bg
(Box b, Point plocal, tb_attr attr);

bool check_attr
(Point p, tb_attr attr, bool fg, bool bg);
bool check_attr
(Box b, Point plocal, tb_attr attr, bool fg, bool bg);

int write_string
(Point start, size_t nchar, std::string_view s, tb_attr fg, tb_attr bg);
int write_string
(Point start, size_t nchar, std::string_view s, tb_attr attr);
int write_string
(Point start, size_t nchar, std::string_view s);
int write_string
(Box b, Point local_start, size_t nchar, std::string_view s, tb_attr fg, tb_attr bg);
int write_string
(Box b, Point local_start, size_t nchar, std::string_view s, tb_attr attr);
int write_string
(Box b, Point local_start, size_t nchar, std::string_view s);
int write_string
(Box b, Point local_start, std::string_view s, tb_attr fg, tb_attr bg);
int write_string
(Box b, Point local_start, std::string_view s, tb_attr attr);
int write_string
(Box b, Point local_start, std::string_view s);

// box funcs
Point get_global
(Box b, Point plocal);

Point get_local
(Box b, Point pglobal);

bool is_in
(Box b, Point pglobal);

bool in_bounds
(Box b, Point plocal);

int clear
(Box b, Point plocal);
int clear
(Box b);
}
