#include <string>

#include <sqlite3.h>

struct SqliteErr {
  const int code;
  const std::string msg;
};

struct SqliteConn {
  sqlite3* ptr = nullptr;
  operator sqlite3* () const { return ptr; }  // avoid having to route through a layer to access ptr

  SqliteConn () = default;
  // delete copy, avoid double free/close
  SqliteConn (const SqliteConn&) = delete;
  SqliteConn& operator= (const SqliteConn&) = delete;
  ~SqliteConn () { if (ptr) sqlite3_close (ptr); }  // can err, but shrug
};
