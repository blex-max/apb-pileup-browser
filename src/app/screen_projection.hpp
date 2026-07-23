#pragma once

#include <cstddef>
#include <cstdint>

// Where a genomic coordinate lands when drawing into a box horizontally
// centered on `boxCenterGPos`: how many leading characters of the
// content are off-screen to the left (skipChars), or how many columns
// to shift the draw start right if the content begins after the box's
// left edge (jOffset). Exactly one of the two is ever nonzero.
struct ScreenProjection {
  size_t skipChars = 0;
  int jOffset = 0;
};

inline ScreenProjection project_onto_box (
    int64_t boxCenterGPos, size_t boxWidth, int64_t contentGStart
)
{
  const int64_t leftmostVisibleGPos =
      boxCenterGPos - (static_cast<int64_t> (boxWidth) / 2);
  const int64_t distBoxEdgeToContentStart =
      contentGStart - leftmostVisibleGPos;
  if (distBoxEdgeToContentStart < 0) {
    return {
        .skipChars =
            static_cast<size_t> (-distBoxEdgeToContentStart),
        .jOffset = 0
    };
  }
  return {
      .skipChars = 0,
      .jOffset = static_cast<int> (distBoxEdgeToContentStart)
  };
}
