#pragma once

#include <optional>
#include <string>
#include <string_view>

// Formats a failed sqlite3 return code into a user-facing error.
// `what` is the operation which failed, `conn_msg` an optional
// sqlite3_errmsg from the relevant handle.
std::string apb_describe_sqlite_failure (
    std::string_view what, int rc, std::optional<std::string_view> conn_msg
);
