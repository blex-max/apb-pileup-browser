#include "expand.hpp"

#include <fmt/format.h>

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <string_view>

#ifdef STDERR_DEBUG
#include <iostream>
#define CERR_DEBUG(msg) std::cerr << msg << std::endl;
#else
#define CERR_DEBUG(msg)
#endif

// On my mac, compile command for testing this TU from build/:
// rm expand-tests; clang++ -std=c++23 -DFMT_HEADER_ONLY -DSTDERR_DEBUG \
//   -I ../vendor/fmt/include \
//   -I ../vendor/doctest \
//   ../tests/test.cpp \
//   ../src/generic/expand.cpp \
//   -o expand-tests && ./expand-tests

/* --- apb expansion tokens --- */
// TODO: more notes!

// clang-format off
#define APB_EXPAND_LAMBDA \
  [] (std::string_view trailing, std::string* out) -> incr_else_errmsg_t
// clang-format on

static constexpr apb_expansion_token_t apb_expand_tag{
    "tag",
    APB_EXPAND_LAMBDA {
      /* syntax: tag 'RG' IS... */
      auto fail = [] (std::string_view msg) {
        return std::unexpected<std::string> (
            fmt::format ("Failed to expand '@tag'- {}", msg)
        );
      };

      size_t ic = 0;
      ic += 3;  // jump kw
      while (trailing[ic] == ' ') {
        ++ic;
        if (ic >= trailing.size()) {
          return fail ("expected single quotes, got end of string");
        }
      }
      if (trailing[ic] != '\'') {
        return fail (
            fmt::format ("expected single quotes, got {}", trailing[ic])
        );
      }
      ++ic;
      std::string tagKey;
      while (trailing[ic] != '\'') {
        // suboptimal. Could memcpy...
        tagKey += trailing[ic];
        ++ic;
        if (ic >= trailing.size()) {
          return fail (
              "expected matching ''' (single quote) after opening "
              "quote, got end of string"
          );
        }
      }
      ++ic;  // jump final single quote
      out->append (fmt::format ("tags ->> '$.{}'", tagKey));
      return ic;
    }
};


static auto apb_keywords = []() {
  static auto kw_arr = std::to_array<const apb_expansion_token_t*> ({
      &apb_expand_tag,
  });
  std::sort (
      begin (kw_arr), end (kw_arr), [] (const auto* a, const auto* b) {
        return a->token.size() > b->token.size();
      }
  );
  return kw_arr;
}();


// FIXME: This implementation is a mess rn:
// - allowing the tag token to go `@tag 'XX'` was a mistake.
//   Whitespace separation makes the whole thing way more complicated.
//   Allow only expansion fns with the syntax `@fntok(args[, ...])` and
//   naive substitution tokens like `@subtok`.
//   - Or, put in the effort to figure out how to safely lex whitespace.
//     But I think it would require a pretty full featured lexer to be able to
//    to apply e.g. `@token <compound-conditional-arg>`.
// - lex the inputs more.
//   - `@fntok()` should recieve only the proximate matching bracket pair as input.
//   - `@subtok` recieves nothing.
// - NOTE: I intend to provide the user with the ability to select arbitrary
//   columns for display in the TUI. The most obvious syntax is 'col <cond> [AS <name>],
//   since that matches raw SQL. However since querying is piecewise `name` cannot be
//   a transient alias. I think I can do this by literally materialising the column
//   into the temp table which is used for querying, thereby making it available
//   to subsequent filter/sort operations. If so, users *may* not need to be able
//   to define substitution tokens at runtime, and then we only support built-in
//   expansion tokens (tag, flag, match, feature_at, ...)
// FIXME: shortest match len is taken statically and assumes tokenRegistry
// is presorted...
// FIXME: token matching is too naive - @foobar matches @foo
// FIXME: other bugs... dropped @, oob read...
// TODO: substitution registry not handled.
// NOTE: token indicator char could be argument to function, but I don't have
// a use case for doing that as of now.
std::string apb_expand_tokens (
    std::string_view s,
    std::span<const apb_expansion_token_t*> expansionRegistry,
    std::span<const apb_substitution_token_t*> substitutionRegistry,
    const token_opts_t opts
)
{
  // "@" is (currently) the selected character for token substitution by apb
  // (implicitly " @", i.e. space-@).
  static constexpr auto apb_expansion_prefix = '@';

  CERR_DEBUG (
      fmt::format ("called apb_expand_tokens on input string {}", s)
  );

  if (s.empty()) {
    return "";
  }
  // add 2 for " @"
  static auto shortest_match_len =
      expansionRegistry.back()->token.size() + 2;
  const auto slen = std::ssize (s);
  if (slen <= shortest_match_len) {
    CERR_DEBUG ("string too short to contain any keywords");
    return std::string{s};
  }

  size_t ic = 0;
  std::string out;
  while (ic < s.size()) {
    // FIXME: suboptimal to build output char by char.
    switch (s[ic]) {
    case ' ': {
      CERR_DEBUG ("found ' ' (space)");
      out += ' ';
      ic++;
      if (s[ic] == apb_expansion_prefix) {
        CERR_DEBUG ("found @ after space");
        ic++;
        // we may have found an apb token.
        // Does a known apb keyword appear after it?
        CERR_DEBUG (fmt::format ("trailing string is: {}", s.substr (ic)));
        for (const auto* regTok : expansionRegistry) {
          CERR_DEBUG (fmt::format ("Trying kw {}", regTok->token));
          if (regTok->token.size() > (s.size() - ic)) {
            continue;
          }
          if (std::strncmp (
                  regTok->token.data(), s.data() + ic, regTok->token.size()
              ) == 0) {
            // we have a match, call fn
            CERR_DEBUG (
                fmt::format (
                    "matched {}; calling expansion fn with ic at {}",
                    regTok->token, ic
                )
            );
            if (const auto ret =
                    regTok->fn (std::string_view{s.data() + ic}, &out);
                !ret) {
              // FIXME: do something of more substance
              // on error. Report message and position in
              // input where error occured.
              CERR_DEBUG (
                  fmt::format ("expansion failed: {}", ret.error())
              );
            }
            else {
              ic += *ret;
            }
            CERR_DEBUG (
                fmt::format (
                    "after expansion, output string is now: {}\nic at: {}",
                    out, ic
                )
            );
            break;
          }
        }
        break;
      }
    }
    default:
      out += s[ic];
      ic++;
      break;
    }
    CERR_DEBUG (
        fmt::format ("at end of cycle, expanded string is {}", out)
    );
  }

  CERR_DEBUG ("");  // insert newline
  return out;
}

#ifndef DOCTEST_CONFIG_DISABLE

#include "doctest.h"

TEST_CASE ("apb_expand_tokens")
{
  // FIXME: these are not tests for apb_expand_tokens
  // They are tests of apb_expand_tag...
  // Should be separate test cases with clear boundaries
  SUBCASE (R"txt( @tag 'xx' is expanded to tags ->> '$.xx' )txt")
  {
    constexpr std::string_view expected =
        R"txt(WHERE tags ->> '$.RG' IS 'sample1';)txt";
    SUBCASE ("proper usage")
    {
      constexpr std::string_view input =
          R"txt(WHERE @tag 'RG' IS 'sample1';)txt";
      CHECK (apb_expand_tokens (input, apb_keywords, {}, {}) == expected);
    }

    SUBCASE ("whitespace irrelevant")
    {
      SUBCASE ("missing space")
      {
        constexpr std::string_view input =
            R"txt(WHERE @tag'RG' IS 'sample1';)txt";
        CHECK (
            apb_expand_tokens (input, apb_keywords, {}, {}) == expected
        );
      }

      SUBCASE ("extra spaces")
      {
        constexpr std::string_view input =
            R"txt(WHERE @tag   'RG' IS 'sample1';)txt";
        CHECK (
            apb_expand_tokens (input, apb_keywords, {}, {}) == expected
        );
      }
    }

    SUBCASE ("@ prefix mandatory")
    {
      constexpr std::string_view input =
          R"txt(WHERE tag 'RG' IS 'sample1';)txt";
      CHECK (apb_expand_tokens (input, apb_keywords, {}, {}) == input);
    }

    // FIXME: add misuse cases
  }
}

#endif  // DOCTEST_CONFIG_DISABLE
