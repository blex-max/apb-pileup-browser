#pragma once

#include <termbox2.h>

#include "shared/apb_assert.hpp"

// as APB_ASSERT,
// but shutdown tb2
#define TUI_ASSERT(expr, ...)                     \
  do {                                            \
    if (expr) [[likely]] {                        \
      /* success, do nothing */                   \
    }                                             \
    else [[unlikely]] {                           \
      tb_shutdown();                              \
      abort_fatal (                               \
          __FILE__, __LINE__,                     \
          "assertion failed: " #expr __VA_OPT__ ( \
              , #__VA_ARGS__, __VA_ARGS__         \
          )                                       \
      );                                          \
    }                                             \
  } while (false)

#define TUI_UNREACHABLE(msg, ...)                                         \
  do {                                                                    \
    tb_shutdown();                                                        \
    abort_fatal (                                                         \
        __FILE__, __LINE__, (msg)__VA_OPT__ (, #__VA_ARGS__, __VA_ARGS__) \
    );                                                                    \
  } while (false)
