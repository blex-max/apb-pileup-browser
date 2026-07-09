#include <sqlite3.h>

struct SqliteConn {
  sqlite3* o_ptr = nullptr;
  operator sqlite3* () const { return o_ptr; }  // avoid having to route through a layer to access ptr

  SqliteConn () = default;
  // delete copy, avoid double free/close
  SqliteConn (const SqliteConn&) = delete;
  SqliteConn& operator= (const SqliteConn&) = delete;
  ~SqliteConn () { if (o_ptr) sqlite3_close_v2 (o_ptr); }  // v2: never leaks even if a stmt/backup is still outstanding; can't report an error from a dtor anyway
};
