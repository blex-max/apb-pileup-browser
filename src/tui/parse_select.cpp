#include "parse_select.hpp"

#include <cstddef>
#include <regex>

#include "utility/apb_assert.hpp"
#include "utility/debug.hpp"
#include "utility/stringify.hpp"

namespace {
// NOTE: how does this interact with the
// sqlite option to disable double quotes
// DQS_DDL and DQS_DML?
enum sql_fsm_state_t : uint8_t {
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
}  // namespace

static std::string to_string (const apb_parse_select_err_t& err)
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
  case apb_parse_select_err_t::comment:
    msg += "comment in clause";
    break;
  case apb_parse_select_err_t::imbalancedParens:
    msg += "imbalanced parentheses";
    break;
  case apb_parse_select_err_t::imbalancedQuotes:
    msg += "imbalanced quotes";
    break;
  case apb_parse_select_err_t::unexpectedEoS:
    msg += "unexpeced end of clause";
    break;
  case apb_parse_select_err_t::illegalChar:
    msg += "illegal character in clause";
    break;
  }
  return msg;
}

// SOURCES: sqlite3 select statement and language docs (for quoting)
// and tokenizer.c
// TODO: write up spec
static std::expected<std::vector<std::string_view>, apb_parse_select_err_t>
lex_select (std::string_view apbSelect)
{
#define RET_UNEXP(type, at, ch)                      \
  do {                                               \
    return std::unexpected<apb_parse_select_err_t> ( \
        {apb_parse_select_err_t::type, at, ch}       \
    );                                               \
  } while (0)


  if (apbSelect.empty()) {
    return {};
  }

  const auto selS = apbSelect;
  std::vector<std::string_view> out;
  sql_fsm_state_t state = normal;
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
        break;

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
#undef RET_UNEXP
}

static std::optional<std::string_view> apb_find_alias (
    std::string_view selectToken
)
{
  APB_ASSERT (selectToken.front() != ' ', selectToken.front());
  APB_ASSERT (selectToken.back() != ' ', selectToken.back());

  // A token has a well-formed alias if it matches the following regex:
  // This took a while to figure out!
  static const std::regex re (
      R"re(^(?:(?:(['`"]).*\1|\[.*\])\s*|[^'`" \[].*\s\s*)[aA][sS] *((?:(['"`]).+\3|\[.+\]| [^"'`\[]+))$)re"
  );

  std::match_results<std::string_view::const_iterator> m;
  std::string_view out;
  if (std::regex_match (begin (selectToken), end (selectToken), m, re)) {
    // The second capturing group of the regex contains the alias
    out = {m[2].first, m[2].second};
  }
  while (!out.empty() && out.front() == ' ') {
    out.remove_prefix (1);
  }
  if (out.empty()) {
    return std::nullopt;
  }
  return out;
}

std::expected<std::vector<apb_sel_term_t>, apb_parse_select_err_t>
apb_parse_select (std::string_view apbSelectClause)
{
  std::vector<std::string_view> tokens;
  if (const auto ret = lex_select (apbSelectClause); !ret) {
    return std::unexpected (ret.error());
  }
  else {
    tokens = *ret;
  }

  std::vector<apb_sel_term_t> out;
  for (const auto tok : tokens) {
    auto& outElem = out.emplace_back (std::string{tok}, std::nullopt);
    outElem.asAlias = apb_find_alias (outElem.tok);
  }
  return out;
}


#ifndef DOCTEST_CONFIG_DISABLE

#include "doctest.h"

template <>
struct doctest::StringMaker<std::vector<std::string_view>> {
  static String convert (const std::vector<std::string_view>& toks)
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
struct doctest::StringMaker<apb_parse_select_err_t> {
  static String convert (const apb_parse_select_err_t& err)
  {
    return to_string (err).c_str();
  }
};

template <>
struct doctest::StringMaker<
    std::expected<std::vector<std::string_view>, apb_parse_select_err_t>> {
  static String convert (
      const std::expected<
          std::vector<std::string_view>, apb_parse_select_err_t>& ex
  )
  {
    return ex ? StringMaker<std::vector<std::string_view>>::convert (*ex)
              : StringMaker<apb_parse_select_err_t>::convert (ex.error());
  }
};

TEST_CASE (NAME_OF (lex_select_terms_sql))
{
#define REQUIRE_TOKS(inStr, ...)                                 \
  do {                                                           \
    const auto ret = lex_select (inStr);                         \
    REQUIRE (ret);                                               \
    REQUIRE (*ret == std::vector<std::string_view> __VA_ARGS__); \
  } while (0)

#define REQUIRE_ERR(inStr, type, at, hint)                      \
  do {                                                          \
    const auto ret = lex_select (inStr);                        \
    REQUIRE (!ret);                                             \
    REQUIRE (ret.error().type == apb_parse_select_err_t::type); \
    REQUIRE (ret.error().column == (at));                       \
    REQUIRE (ret.error().ch == (hint));                         \
  } while (0)

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
#undef REQUIRE_TOKS
#undef REQUIRE_ERR
}

template <>
struct doctest::StringMaker<std::nullopt_t> {
  static String convert (const std::nullopt_t&) { return "nullopt"; }
};

template <>
struct doctest::StringMaker<std::optional<std::string_view>> {
  static String convert (const std::optional<std::string_view>& ex)
  {
    return ex ? ex.value().data() : "nullopt";
  }
};

TEST_CASE (NAME_OF (apb_find_alias))
{
  SUBCASE ("empty returns nullopt")
  {
    REQUIRE (apb_find_alias ("") == std::nullopt);
  }

  SUBCASE ("no AS alias returns nullopt")
  {
    REQUIRE (apb_find_alias ("a") == std::nullopt);
    REQUIRE (apb_find_alias ("colasname") == std::nullopt);
    REQUIRE (apb_find_alias ("'col AS weird name'") == std::nullopt);
    REQUIRE (apb_find_alias ("col alias_without_AS") == std::nullopt);
  }

  SUBCASE ("malformed alias returns nullopt")
  {
    REQUIRE (apb_find_alias ("col AS") == std::nullopt);
    REQUIRE (apb_find_alias ("[col AS x") == std::nullopt);
    REQUIRE (apb_find_alias ("'col] AS x") == std::nullopt);
    REQUIRE (apb_find_alias ("'col AS x") == std::nullopt);
    REQUIRE (apb_find_alias ("col AS 'x") == std::nullopt);
    REQUIRE (apb_find_alias ("col AS [x") == std::nullopt);
  }

  SUBCASE ("bare alias")
  {
    REQUIRE (apb_find_alias ("col AS x") == "x");
    REQUIRE (apb_find_alias ("col as x") == "x");
    REQUIRE (apb_find_alias ("col aS x") == "x");
    REQUIRE (apb_find_alias ("col AS   x") == "x");
  }

  SUBCASE ("quoted and bracketed aliases keep delimiters")
  {
    REQUIRE (apb_find_alias ("col AS 'x y'") == "'x y'");
    REQUIRE (apb_find_alias ("col AS \"x y\"") == "\"x y\"");
    REQUIRE (apb_find_alias ("col AS `x y`") == "`x y`");
    REQUIRE (apb_find_alias ("col AS [x y]") == "[x y]");
    REQUIRE (apb_find_alias ("col AS'x'") == "'x'");
    REQUIRE (apb_find_alias ("col AS[x]") == "[x]");
  }

  SUBCASE ("quoted and bracketed columns")
  {
    REQUIRE (apb_find_alias ("'col name' AS x") == "x");
    REQUIRE (apb_find_alias ("\"col name\" AS x") == "x");
    REQUIRE (apb_find_alias ("`col name` AS x") == "x");
    REQUIRE (apb_find_alias ("[col name] AS 'x y'") == "'x y'");
    REQUIRE (apb_find_alias ("'col'AS x") == "x");
  }

  SUBCASE ("complex expressions")
  {
    REQUIRE (apb_find_alias ("instr(c, 'ACTG') AS foo") == "foo");
    REQUIRE (apb_find_alias ("count(*) AS n") == "n");
    REQUIRE (apb_find_alias ("a AS b AS c") == "c");
  }
}

#endif
