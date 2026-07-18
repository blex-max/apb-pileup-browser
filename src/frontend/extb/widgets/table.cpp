#include "table.hpp"

#include <cassert>
#include <cstddef>

#include "frontend/extb/extb.hpp"

namespace table {

void draw_table (
    const extb::Box& b,
    std::vector<std::vector<std::string>> cols,
    std::vector<std::string> headers
)
{
  // always clear
  extb::clear (b);

  if (cols.empty()) {
    return;
  }

  const auto javail = extb::size (b.jspan);
  const auto nrow = cols[0].size();
  int jcurs = 0;

  /* write table col by col */
  for (size_t col_idx = 0; col_idx < cols.size(); ++col_idx) {
    const auto& col = cols[col_idx];
    assert (col.size() == nrow);

    size_t max_width = 0;  // track width
    int icurs = 0;  // loop var over rows

    if (!headers.empty()) {
      assert (cols.size() == headers.size());
      const auto head = headers[col_idx];
      max_width =
          head.size();  // set max col width based on heading
      extb::write_string (
          {b.ispan.first + icurs, b.jspan.first + jcurs},
          b.jspan.last, head
      );
      icurs += 2;
    }

    for (const auto& entry : col) {
      const auto nchar = entry.size();
      if (nchar > max_width) {
        max_width = nchar;
      }
      extb::write_string (
          {b.ispan.first + icurs, b.jspan.first + jcurs},
          b.jspan.last, entry
      );
      ++icurs;
    }

    jcurs +=
        max_width +
        1;  // move cursor to next start, leaving space for sep

    if (jcurs > javail) {
      // available width filled
      break;
    }

    // set sep behind cursor
    extb::set (
        extb::ILine{b.ispan, b.jspan.first + jcurs - 1}, 0x2502
    );
  }
}

}  // end namespace table
