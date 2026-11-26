// NULL-aware sqlite3 column readers
#pragma once

#include <sqlite3.h>

#include <optional>
#include <string>

// nullopt if SQLITE_NULL. asserts type is text otherwise
std::optional<std::string> col_text (sqlite3_stmt* stmt, int idx);

// nullopt if SQLITE_NULL, NA if sqlite blob
std::optional<std::string> col_to_string (sqlite3_stmt* stmt, int idx);
