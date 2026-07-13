#pragma once

/*
table widget for displaying data
based on extb/termbox2

A table is a series of columns of
variable width with a header for each
column

A table does not hold data

termbox2 doesn't require redraw each frame

A table then can be a stateless drawing function

for the number of queries * cols it is possible to
realistically view on screen at any one time,
it probably doesn't matter if the program iterates
over data each row

A table drawing function could take
a vector of pairs of headers and callbacks to extract
the necessary data from the data structure, and
col width. And take a vector of the data
structures

but then the table wouldn't be generic
unless void pointers were used

regardless, drawing should stay separate from data
management
*/

#include <vector>

#include "extb/extb-box.hpp"

namespace table {

void draw_table(
    const extb::box::GlobalBox& b,
    std::vector<std::vector<std::string>> cols,
    std::vector<std::string> headers
);

}
