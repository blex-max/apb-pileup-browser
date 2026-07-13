#pragma once

#include <cstddef>

#include "extb.hpp"

namespace extb {
namespace box {
using namespace extb;

struct Span {
  int first = -1, last = -1;

    // member function as a concession to
    // cpp as size () looks markedly odd
    // as a free function.
  size_t size() const noexcept;
};
struct GlobalSpan : public Span {};
struct LocalSpan : public Span {};
bool contains(const Span& s, int p) noexcept;
bool contains(
    const GlobalSpan& outer, const GlobalSpan& inner
) noexcept;
bool contains(
    const LocalSpan& outer, const LocalSpan& inner
) noexcept;
int last_local(const GlobalSpan& s) noexcept;
bool valid(const Span& s) noexcept;

struct GlobalBox {
    // default-construct invalid
    GlobalSpan ispan;
    GlobalSpan jspan;
};
// box factories
GlobalBox make_box(
    const GlobalSpan& ispan, const GlobalSpan& jspan
);
GlobalBox make_row(int i, const GlobalSpan& jspan);
GlobalBox make_col(const GlobalSpan& ispan, int j);

// box methods
LocalCell last_local(const GlobalBox& b) noexcept;
GlobalCell to_global(const GlobalBox& b, LocalCell c);
GlobalCell top_left(const GlobalBox& b) noexcept;
GlobalCell top_right(const GlobalBox& b) noexcept;
GlobalCell bottom_left(const GlobalBox& b) noexcept;
GlobalCell bottom_right(const GlobalBox& b) noexcept;
bool valid(const GlobalBox& b) noexcept;

// interface for extb
class BoxGlobalCellsView
    : public std::ranges::view_interface<BoxGlobalCellsView> {
 public:
  explicit BoxGlobalCellsView(GlobalBox box) : box_(box) {}

  class iterator {
   public:
    using iterator_concept = std::forward_iterator_tag;
    using value_type = GlobalCell;
    using difference_type = std::ptrdiff_t;

    iterator() = default;

    iterator(GlobalBox box, std::size_t index)
        : box_(box), index_(index)
    {
    }

    GlobalCell operator*() const
    {
      const std::size_t width = box_.jspan.size();
      const int di = static_cast<int>(index_ / width);
      const int dj = static_cast<int>(index_ % width);
      return GlobalCell{
          box_.ispan.first + di, box_.jspan.first + dj
      };
    }

    iterator& operator++()
    {
      ++index_;
      return *this;
    }

    iterator operator++(int)
    {
      auto tmp = *this;
      ++(*this);
      return tmp;
    }

    friend bool operator==(const iterator& a, const iterator& b)
    {
      return a.index_ == b.index_;
    }

   private:
    GlobalBox box_{};
    std::size_t index_ = 0;
  };

  iterator begin() const { return iterator{box_, 0}; }

  iterator end() const
  {
    return iterator{box_, box_.ispan.size() * box_.jspan.size()};
  }

 private:
  GlobalBox box_;
};

inline auto cell_source(GlobalBox box)
{
  return BoxGlobalCellsView{box};
}

}  // end namespace box
}  // end namespace extb
