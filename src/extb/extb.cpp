#include "extb.hpp"

#include <cassert>

namespace extb {

bool contains_global (const GlobalBox& b, const GlobalCell& cglobal) noexcept {
  return cglobal.i >= b.ispan.first && cglobal.i <= b.ispan.last && cglobal.j >= b.jspan.first && cglobal.j <= b.jspan.last;
}
bool contains_local (const GlobalBox& b, const LocalCell& clocal) noexcept {
  return valid(clocal) && clocal.i <= last_local(b).i && clocal.j <= last_local(b).j;
}

bool valid (const Cell& c) noexcept {
  return (c.i >= 0 && c.j >= 0);
}
bool valid (const Span& s) noexcept {
  return (s.first >= 0 && s.last >= 0 && s.last >= s.first);
}
bool valid (const GlobalBox& b) noexcept {
  return valid (b.ispan) && valid (b.jspan);
}

// Box factories
GlobalBox make_box (const GlobalSpan& ispan, const GlobalSpan& jspan) {
  GlobalBox b{ispan.first, ispan.last, jspan.first, jspan.last};
  if (valid (b)) {
    return b;
  }
  else {
    return {};
  }
}
GlobalBox make_row (int i, const GlobalSpan& jspan) {
  return make_box ({i, i}, jspan);
}
GlobalBox make_col (const GlobalSpan& ispan, int j) {
  return make_box (ispan, {j, j});
}
int last_local (const GlobalSpan& s) {
  return s.last - s.first;
};
LocalCell last_local (const GlobalBox& b) noexcept {
  return {
    last_local (b.ispan),
    last_local (b.jspan)
  };
}

// Span methods
size_t Span::size () const noexcept {
    return valid (*this) ? last - first + 1 : 0;
}
bool contains (const Span& s, int p) noexcept {
    return valid (s) && (p >= s.first && p <= s.last);
}
bool contains (const GlobalSpan& outer, const GlobalSpan& inner) noexcept {
    return valid (outer) && valid (inner) && (outer.first <= inner.first && outer.last >= inner.last);
}
bool contains (const LocalSpan& outer, const LocalSpan& inner) noexcept {
    return valid (outer) && valid (inner) && (outer.first <= inner.first && outer.last >= inner.last);
}

GlobalCell to_global
(GlobalBox b, LocalCell c) {
  if (contains_local(b, c)) {
    return { c.i + b.ispan.first, c.j + b.jspan.first};
  }
  else {
    return {};
  }
}


// returns nchars written
size_t write_string
(const GlobalCell& start, std::string_view s, const Style& style) {
  if (!valid (start) || s.empty()) {
    return 0;
  }
  int nout = 0;
  for (size_t j = 0; j < s.size(); ++j) {
    const auto rc = set (
      GlobalCell{start.i, static_cast<int> (start.j + j)},
      s[j],
      style
    );
    if (rc != TB_OK) {
      break;
    }
    ++nout;
  }
  return nout;
}
size_t write_string_within
(const GlobalCell &start, const GlobalBox &b, std::string_view s, const Style& style) {
  if (!valid (start) || s.empty() || !contains_global (b, start)) {
    return 0;
  }

  const auto lim =
    std::min<size_t> (b.jspan.size() - start.j, s.size());

  int nout = 0;
  for (size_t j = 0; j < lim; ++j) {
    const auto rc = set (
      GlobalCell{start.i, static_cast<int> (start.j + j)},
      s[j],
      style
    );
    if (rc != TB_OK) {
      break;
    }
    ++nout;
  }
  return nout;
}

}  // end namespace
