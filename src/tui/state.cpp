#include "state.hpp"

#include <sqlite3.h>

#include "query.hpp"
#include "termbox2.h"

namespace tui_state {

void tui_shutdown()
{
  run = false;
  sqlite3_finalize (db::selectStmt);
  tb_shutdown();
}

}  // namespace tui_state
