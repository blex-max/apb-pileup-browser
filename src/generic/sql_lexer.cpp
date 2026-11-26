
#include <expected>
#include <optional>
#include <string_view>
#include <vector>

#include "utility/stringify.hpp"

#ifdef STDERR_DEBUG
#include <iostream>
#define CERR_DEBUG(msg) std::cerr << msg << std::endl;
#else
#define CERR_DEBUG(msg)
#endif

using tokens_t = std::vector<std::string_view>;

struct lex_select_err_t {
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
static std::string to_string (const lex_select_err_t& err)
{
  std::string msg = "Error in select clause at column ";
  msg += std::to_string (err.column);
  if (err.ch) {
    msg += " (at character '";
    msg += *err.ch;
    msg += "'): ";
  }
  else {
    msg += ": ";
  }
  switch (err.type) {
  case lex_select_err_t::comment:
    msg += "comment in clause";
    break;
  case lex_select_err_t::imbalancedParens:
    msg += "imbalanced parentheses";
    break;
  case lex_select_err_t::imbalancedQuotes:
    msg += "imbalanced quotes";
    break;
  case lex_select_err_t::unexpectedEoS:
    msg += "unexpeced end of clause";
    break;
  case lex_select_err_t::illegalChar:
    msg += "illegal character in clause";
    break;
  }
  return msg;
}


// SOURCES: sqlite3 select statement and language docs (for quoting)
// and tokenizer.c
// TODO: write up spec
static std::expected<tokens_t, lex_select_err_t> apb_lex_select (
    std::string_view apbSelect
)
{
#define RET_UNEXP(type, at, ch)                \
  do {                                         \
    return std::unexpected<lex_select_err_t> ( \
        {lex_select_err_t::type, at, ch}       \
    );                                         \
  } while (0)

  if (apbSelect.empty()) {
    return {};
  }

  const auto selS = apbSelect;
  tokens_t out;
  enum state_t : uint8_t {
    normal,
    // These are the four ways
    // you can "quote" a
    // string or column identifier
    // in sqlite.
    inSingleQuotes,
    inDoubleQuotes,
    inBackticks,
    inSquares
  };
  state_t state = normal;
  int parenDepth = 0;
  size_t curTokStart = 0;
  size_t i = 0;
  while (i < selS.size()) {
    const auto& ch = selS[i];
    switch (ch) {
      // shared between modes
      // TODO: control chars?
    case '\t':
    case '\n':
    case '\r':
    case '\0':
      RET_UNEXP (illegalChar, i, std::nullopt);
    default:
      break;
    }
    switch (state) {
    case normal:
      switch (ch) {
      case ';':
        RET_UNEXP (illegalChar, i, std::nullopt);

      // brackets
      // We split only on top-level tokens, i.e.
      // only when depth = 0, hence depth is
      // tracked.
      case '(':
        parenDepth++;
        break;
      case ')':
        parenDepth--;
        break;

      case '`':
        state = inBackticks;
        break;
      case '\'':
        state = inSingleQuotes;
        break;
      case '"':
        state = inDoubleQuotes;
        break;
      case '[':
        state = inSquares;
        break;
      case ']':
        RET_UNEXP (imbalancedQuotes, i, "]");

      // comments
      case '/':
        // Since comments are parse errors, I think we
        // can just ignore closing */. The opening /*
        // should always fail.
        if (i + 1 >= selS.size()) {
          RET_UNEXP (unexpectedEoS, i, std::nullopt);
        }
        if (selS[i + 1] == '*') {
          // comment, error
          RET_UNEXP (comment, i, "/*");
        }
        break;
      case '-':
        if (i + 1 >= selS.size()) {
          RET_UNEXP (unexpectedEoS, i, std::nullopt);
        }
        if (selS[i + 1] == '-') {
          RET_UNEXP (comment, i, "--");
        }

      // comma, for detecting complete top-level tokens
      case ',':
        if (parenDepth == 0) {
          // try push a token onto output
          std::string_view tok = {
              begin (selS) + curTokStart, begin (selS) + i
          };
          // trim whitespace
          while (!tok.empty() && tok.front() == ' ') {
            tok.remove_prefix (1);
          }
          while (!tok.empty() && tok.back() == ' ') {
            tok.remove_suffix (1);
          }
          // empty token is ignored
          if (!tok.empty()) {
            out.emplace_back (tok);
          }
          i++;
          curTokStart = i;
          continue;
        }
        break;

      default:
        break;
      }
      break;
    case inSingleQuotes:
      switch (ch) {
      case '\'':
        state = normal;
        break;
      default:
        break;
      }
      break;
    case inDoubleQuotes:
      switch (ch) {
      case '"':
        state = normal;
        break;
      default:
        break;
      }
      break;
    case inBackticks:
      switch (ch) {
      case '`':
        state = normal;
        break;
      default:
        break;
      }
      break;
    case inSquares:
      switch (ch) {
      // sqlite does not have nesting of square brackets
      // A column quoted like [a[b] is named `a[b`
      // So we only check for right square.
      // [a]b] is always invalid SQL in sqlite and is
      // not a valid quoted identifier.
      case ']':
        state = normal;
        break;
      default:
        break;
      }
      break;
    }
    if (parenDepth < 0) {
      RET_UNEXP (imbalancedParens, i, ")");
    }
    i++;
  }

  // terminal conditions
  if (state != normal) {
    RET_UNEXP (imbalancedQuotes, i, std::nullopt);
  }
  if (parenDepth != 0) {
    CERR_DEBUG ("paren depth is:");
    CERR_DEBUG (std::to_string (parenDepth));
    RET_UNEXP (imbalancedParens, i, std::nullopt);
  }

  {
    // collect terminal token
    std::string_view tok = {begin (selS) + curTokStart, begin (selS) + i};
    // trim whitespace
    while (!tok.empty() && tok.front() == ' ') {
      tok.remove_prefix (1);
    }
    while (!tok.empty() && tok.back() == ' ') {
      tok.remove_suffix (1);
    }
    // empty token is ignored
    if (!tok.empty()) {
      out.emplace_back (tok);
    }
  }

  return out;
}


#ifndef DOCTEST_CONFIG_DISABLE

#include "doctest.h"

template <>
struct doctest::StringMaker<tokens_t> {
  static String convert (const tokens_t& toks)
  {
    std::string out = "{";
    for (size_t i = 0; i < toks.size(); i++) {
      if (i != 0) {
        out += ", ";
      }
      out += '"';
      out += toks[i];
      out += '"';
    }
    out += "}";
    return out.c_str();
  }
};

template <>
struct doctest::StringMaker<lex_select_err_t> {
  static String convert (const lex_select_err_t& err)
  {
    return to_string (err).c_str();
  }
};

template <>
struct doctest::StringMaker<std::expected<tokens_t, lex_select_err_t>> {
  static String convert (
      const std::expected<tokens_t, lex_select_err_t>& ex
  )
  {
    return ex ? StringMaker<tokens_t>::convert (*ex)
              : StringMaker<lex_select_err_t>::convert (ex.error());
  }
};

#define REQUIRE_TOKS(inStr, ...)             \
  do {                                       \
    const auto ret = apb_lex_select (inStr); \
    REQUIRE (ret);                           \
    REQUIRE (*ret == tokens_t __VA_ARGS__);  \
  } while (0)

#define REQUIRE_ERR(inStr, type, at, hint)                \
  do {                                                    \
    const auto ret = apb_lex_select (inStr);              \
    REQUIRE (!ret);                                       \
    REQUIRE (ret.error().type == lex_select_err_t::type); \
    REQUIRE (ret.error().column == (at));                 \
    REQUIRE (ret.error().ch == (hint));                   \
  } while (0)

TEST_CASE (NAME_OF (lex_select_terms_sql))
{
  SUBCASE ("empty returns empty") { REQUIRE_TOKS ("", {}); }

  SUBCASE ("Illegal char is error")
  {
    REQUIRE_ERR ("illegal \t", illegalChar, 8, std::nullopt);
  }

  SUBCASE ("/* comment is error")
  {
    REQUIRE_ERR (
        "a,b, /* comment */ c, d", comment, 5, std::make_optional ("/*")
    );
  }

  SUBCASE ("/* comment inside quotes is ignored")
  {
    REQUIRE_TOKS ("a, 'n/*ame'", {"a", "'n/*ame'"});
  }

  SUBCASE ("-- comment is error")
  {
    REQUIRE_ERR ("a,b, -- comment", comment, 5, std::make_optional ("--"));
  }

  SUBCASE ("-- comment inside quotes is ignored")
  {
    REQUIRE_TOKS ("a, 'n--ame'", {"a", "'n--ame'"});
  }

  SUBCASE ("imbalanced brackets is error")
  {
    constexpr std::string_view in = "((a,b,c)";
    REQUIRE_ERR (in, imbalancedParens, in.size(), std::nullopt);
  }

  SUBCASE ("imbalanced quotes is error")
  {
    constexpr std::string_view in = "'a,b,c''";
    REQUIRE_ERR (in, imbalancedQuotes, in.size(), std::nullopt);
  }

  SUBCASE ("tokenisation tests")
  {
    SUBCASE ("unit tokens") { REQUIRE_TOKS ("a,b,c", {"a", "b", "c"}); }
    SUBCASE ("aliased columns")
    {
      REQUIRE_TOKS ("a, b AS foo, c bar", {"a", "b AS foo", "c bar"});
    }
    SUBCASE ("complex tokens")
    {
      REQUIRE_TOKS (
          "a, b > 3, instr(c, 'ACTG') AS foo, substr(d, 1, 10) GLOB "
          "'A[GT]C'",
          {"a", "b > 3", "instr(c, 'ACTG') AS foo",
           "substr(d, 1, 10) GLOB 'A[GT]C'"}
      );
    }
  }
}

#endif
