#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>


struct apb_sel_term_t {
  // raw term
  std::string tok;
  // view into `tok` of sql column alias
  // after top-level AS, if present.
  // Else nullopt.
  std::optional<std::string_view> asAlias;
};
struct apb_parse_select_err_t {
  enum type_t : uint8_t {
    comment,
    imbalancedParens,
    imbalancedQuotes,
    unexpectedEoS,
    illegalChar
  };
  type_t type;
  size_t column = 0;
  std::optional<std::string> ch = std::nullopt;
};

// Parse an SQL select clause into terms, identifying if each
// term has a column alias name as indicated by a top-level AS.
// Expects only a select clause; must not be followed by WHERE, ORDER BY, etc.
// The clause must have no leading or trailing whitespace, or leading SQL keywords
// (e.g. SELECT, DISTINCT), etc.
//
// On failure to lex input into tokens, reports error indicating problem
// in input.
//
// token validity is up to the caller. Success of the function in parsing
// the input does not imply valid SQL.
//
// NOTE: could attempt to prepare statement before lexing, without stepping.
// If preparation succeeds, then statement is valid SQL and parsing
// should succeed if code is well-formed.
std::expected<std::vector<apb_sel_term_t>, apb_parse_select_err_t>
apb_parse_select (std::string_view apbSelectClause);
