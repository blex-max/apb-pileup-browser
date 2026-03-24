#include "table.hpp"

#include <cassert>
#include <cstddef>

#include "extb.hpp"

namespace table {

void draw_table (
  const extb::Box& b,
  std::vector<std::vector<std::string>> cols,
  std::vector<std::string> headers
) {
  // always clear
  extb::clear (b);

  if (cols.empty()) {
    return;
  }

  const auto i_avail = b.i().size();
  const auto j_avail = b.j().size();
  const auto nrow = cols[0].size();
  int j_curs = 0;
  for (size_t col_idx = 0; col_idx < cols.size(); ++col_idx) {
    const auto& col = cols[col_idx];
    assert (col.size() == nrow);

    size_t max_width = 0;  // track width
    int i_curs = 0;  // loop var over rows

    if (!headers.empty()) {
      assert (cols.size() == headers.size());
      const auto head = headers[col_idx];
      max_width = head.size();
      extb::write_string(b, {i_curs, j_curs}, head);
      i_curs += 2;
    }

    for (const auto& entry: col) {
      const auto nchar = entry.size();
      if (nchar > max_width) {
        max_width = nchar;
      }
      extb::write_string(b, {i_curs, j_curs}, entry);
      ++i_curs;
    }

    j_curs += max_width + 1;  // move cursor to next start, leaving space for sep

    if (j_curs > j_avail) {
      // available width filled
      break;
    }

    // set sep behind cursor
    for (int i=0; i < i_avail; ++i) {
      extb::set_cell(b, {i, j_curs - 1}, 0x2502);
    }
  }
}

}
