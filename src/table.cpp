#include "table.hpp"

#include <cassert>
#include <cstddef>

#include "extb/extb.hpp"
#include "plog/Log.h"

namespace table {

void draw_table (
  const extb::box::GlobalBox& b,
  std::vector<std::vector<std::string>> cols,
  std::vector<std::string> headers
) {
  // always clear
  extb::clear (b);

  if (cols.empty()) {
    return;
  }

  // PLOGD << "made it";

  const auto iavail = b.ispan.size();
  const auto javail = b.jspan.size();
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
      max_width = head.size();  // set max col width based on heading
      extb::write_string (
        {b.ispan.first + icurs, b.jspan.first + jcurs},
        b.jspan.last,
        head
      );
      icurs += 2;
    }

    for (const auto& entry: col) {
      const auto nchar = entry.size();
      if (nchar > max_width) {
        max_width = nchar;
      }
      extb::write_string (
        {b.ispan.first + icurs, b.jspan.first + jcurs},
        b.jspan.last,
        entry
      );
      ++icurs;
    }

    jcurs += max_width + 1;  // move cursor to next start, leaving space for sep

    if (jcurs > javail) {
      // available width filled
      break;
    }

    // set sep behind cursor
    for (int i=0; i < iavail; ++i) {
      extb::set (
        extb::GlobalCell{b.ispan.first + i, b.jspan.first + jcurs - 1},
        0x2502
      );
    }
  }
}

}  // end namespace table
