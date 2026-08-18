#pragma once

#include <cstdint>

enum boxch : uint16_t {
  topLeftRoundCorner = 0x256D,
  topRightRoundCorner = 0x256E,
  horzLine = 0x2500,
  horzHeavy = 0x2501,
  vertLine = 0x2502,
  bottomLeftRoundCorner = 0x2570,
  bottomRightRoundCorner = 0x256F,
  downTConnect = 0x252C,
  upTConnect = 0x2534,
  leftTConnect = 0x2524,
  rightTConnect = 0x251C,
  cross = 0x253C,
  heavyCross = 0x254B,
};

// combining marks
// for EGC
enum markch : uint16_t {
  ringAbove = 0x030A,
  caron = 0x030C,
  circumf = 0x0302
};
