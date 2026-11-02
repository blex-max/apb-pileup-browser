#pragma once

#include <cstdint>

namespace tui_ch {

constexpr uint32_t missing = 0x00BF;  // inverted question mark

enum box : uint16_t {
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

enum indicator : uint16_t {
  insAt = '^',
  insRingAbove = 0x030A,
};

// fine range 0-46, steps of 5 offset by 10
enum sparkline : uint16_t {
  oneEighth = 0x2581,    // 0-15
  twoEighth = 0x2582,    // 16-20
  threeEighth = 0x2583,  // 21-25
  fourEighth = 0x2584,   // 26-30
  fiveEighth = 0x2585,   // 31-35
  sixEighth = 0x2586,    // 36-40
  sevenEighth = 0x2587,  // 41-45
  full = 0x2588,         // 46-max
};
constexpr auto qual_to_sparkline (uint8_t asciiQual)
{
  const auto qual = asciiQual - 33;
  if (qual < 16) {
    return oneEighth;
  }
  if (qual < 21) {
    return twoEighth;
  }
  if (qual < 26) {
    return threeEighth;
  }
  if (qual < 31) {
    return fourEighth;
  }
  if (qual < 36) {
    return fiveEighth;
  }
  if (qual < 41) {
    return sixEighth;
  }
  if (qual < 46) {
    return sevenEighth;
  }
  return full;
}

}  // namespace tui_ch
