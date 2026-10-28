#include "run.hpp"

#include <cstdlib>

#include "event.hpp"
#include "query.hpp"
#include "shared/cleanup.hpp"
#include "state.hpp"
#include "tui_assert.hpp"
#include "widgets.hpp"

namespace e2 = extb;
namespace g_ui = tui_state::ui;
namespace g_db = tui_state::db;

int run_tui (sqlite3* conn, bool verboseLog)
{
  g_db::conn = conn;
  g_db::locusInfo = get_locus_data (g_db::conn);
  TUI_ASSERT (g_db::locusInfo.valid(), g_db::locusInfo.to_string());
  if (g_db::set_query ({}) != g_db::SetQueryStatus::success) {
    TUI_UNREACHABLE (
        "Failed to set initial query", sqlite3_errmsg (g_db::conn)
    );
  }

  g_ui::cmd::msgBuf =
      "Welcome to apb! All coordinate data is in 1-based closed "
      "coordinates.";

  // init termbox2!
  //
  // Since this may fail, one might think it
  // should be checked before doing any pileup work. But since
  // it is reasonably unlikely to fail and will affect the
  // terminal as soon as tb_init is called, it's best to leave it here.
  if (setlocale (LC_ALL, "") == nullptr) {
    std::cerr << "Warning: could not set locale from environment; "
                 "continuing with the default locale. Non-ASCII "
                 "characters may not display correctly."
              << std::endl;
  }
  if (const auto rc = tb_init(); rc != TB_OK) {
    // Most tb_init failure codes lose their errno text
    // before we see them; word manually.
    switch (rc) {
      case TB_ERR_INIT_ALREADY:
      case TB_ERR_MEM:
        TUI_UNREACHABLE ("tb_init failed", rc, tb_strerror (rc));
      case TB_ERR_INIT_OPEN:
        std::cerr << fmt::format (
                         "Error: could not open the terminal ({}). "
                         "Is apb running in an interactive terminal?",
                         tb_strerror (rc)
                     )
                  << std::endl;
        return EXIT_FAILURE;
      case TB_ERR_NO_TERM:
      case TB_ERR_UNSUPPORTED_TERM:
        // FIXME: what values of TERM does apb support?? Document.
        std::cerr << fmt::format (
                         "Error: {}. Try a different terminal, or "
                         "set TERM to something apb supports (e.g. "
                         "xterm).",
                         tb_strerror (rc)
                     )
                  << std::endl;
        return EXIT_FAILURE;
      default:
        std::cerr << fmt::format (
                         "Error: failed to initialise the terminal "
                         "(code {}). Try again, or in a different "
                         "terminal - if this persists, please "
                         "report it to the maintainer.",
                         rc
                     )
                  << std::endl;
        return EXIT_FAILURE;
    }
  }
  // NOTE: cleanup should be invoked before
  // writing to stderr.
  Defer tb_cleanup ([]() { tui_state::tui_shutdown(); });
  tb_set_input_mode (TB_INPUT_ALT);
  tb_clear();

  {
    // render first frame
    if (!size_main_widgets()) {
      tb_cleanup.invoke();
      std::cerr << "Terminal too small to display TUI! Try resizing?"
                << std::endl;
      return EXIT_FAILURE;
    }
    draw_main_to_buffer();

    e2::write_string (
        first (g_ui::cmd::inputLine), last (g_ui::cmd::inputLine.xspan),
        /* startup help msg */
        "type here - try `help`", {.fg = TB_DIM}
    );
    if (const auto rc = tb_present(); rc != TB_OK) {
      if (rc != TB_ERR) {
        TUI_UNREACHABLE ("tb_present failed", rc, tb_strerror (rc));
      }
      const auto msg = fmt::format (
          "lost connection to the terminal ({}). Try running apb "
          "again.",
          tb_strerror (rc)
      );
      tb_cleanup.invoke();
      std::cerr << "Error: " << msg << std::endl;
      return EXIT_FAILURE;
    }
  }

  tb_event ev{};
  while (tui_state::run) {
    tb_clear();

    tb_poll_event (&ev);
    // NOTE: input still processed if ui
    // too small, to allow quitting.
    handle_event (ev);
    {
      if (g_ui::tooSmall) {
        // hold until a resize succeeds
        e2::write_string (
            e2::GlobalCell{0, 0}, tb_width(), "terminal too small",
            {.fg = TB_DIM}
        );
      }
      else {
        draw_main_to_buffer();
        if (g_ui::showOverlay) {
          // For help overlays
          draw_overlay_to_buffer();
        }
      }
      if (const auto rc = tb_present(); rc != TB_OK) {
        if (rc != TB_ERR) {
          TUI_UNREACHABLE ("tb_present failed", rc, tb_strerror (rc));
        }
        const auto msg = fmt::format (
            "lost connection to the terminal ({}). Try running apb "
            "again.",
            tb_strerror (rc)
        );
        tb_cleanup.invoke();
        std::cerr << "Error: " << msg << std::endl;
        return EXIT_FAILURE;
      }
    }
  }
  tb_cleanup.invoke();

  if (verboseLog) {
    std::cerr << fmt::format (
                     "sqlite3 max memory usage: {} bytes",
                     sqlite3_memory_highwater (0)
                 )
              << std::endl;
  }

  std::cerr << "Bye!" << std::endl;
  return EXIT_SUCCESS;
}
