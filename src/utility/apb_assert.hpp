#pragma once

#include "abort_fatal.hpp"

// will print name and value of any
// variable provided to ...
// Cannot use commas in expr without enclosing expr in brackets!
#define APB_ASSERT(expr, ...)                     \
  do {                                            \
    if (expr) [[likely]] {                        \
      /* success, do nothing */                   \
    }                                             \
    else [[unlikely]] {                           \
      apb_abort_fatal (                           \
          __FILE__, __LINE__,                     \
          "assertion failed: " #expr __VA_OPT__ ( \
              , #__VA_ARGS__, __VA_ARGS__         \
          )                                       \
      );                                          \
    }                                             \
  } while (false)

#define APB_UNREACHABLE(msg, ...)                                         \
  do {                                                                    \
    apb_abort_fatal (                                                     \
        __FILE__, __LINE__, (msg)__VA_OPT__ (, #__VA_ARGS__, __VA_ARGS__) \
    );                                                                    \
  } while (false)
