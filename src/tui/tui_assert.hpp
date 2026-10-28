#pragma once

#include "shared/abort_fatal.hpp"
#include "state.hpp"

// as APB_ASSERT (shared/apb_assert.hpp),
// but shutdown tb2
#define TUI_ASSERT(expr, ...)                     \
  do {                                            \
    if (expr) [[likely]] {                        \
      /* success, do nothing */                   \
    }                                             \
    else [[unlikely]] {                           \
      tui_state::tui_shutdown();                  \
      apb_abort_fatal (                           \
          __FILE__, __LINE__,                     \
          "assertion failed: " #expr __VA_OPT__ ( \
              , #__VA_ARGS__, __VA_ARGS__         \
          )                                       \
      );                                          \
    }                                             \
  } while (false)

#define TUI_UNREACHABLE(msg, ...)                                         \
  do {                                                                    \
    tui_state::tui_shutdown();                                            \
    apb_abort_fatal (                                                     \
        __FILE__, __LINE__, (msg)__VA_OPT__ (, #__VA_ARGS__, __VA_ARGS__) \
    );                                                                    \
  } while (false)

// NOTE: needs sqlite3.h at the use site
#define TUI_S3_EXPECT(fn, val)                                    \
  do {                                                            \
    TUI_ASSERT (                                                  \
        const auto rc = fn; rc == (val), rc, sqlite3_errstr (rc), \
                            sqlite3_errmsg (tui_state::db::conn)  \
    );                                                            \
  } while (false)
