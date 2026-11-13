#include "expand.hpp"

#include <fmt/format.h>

#include <array>
#include <cstring>
#include <expected>
#include <functional>
#include <map>
#include <string>
#include <string_view>

#ifdef STDERR_DEBUG
#include <iostream>
#define CERR_DEBUG(msg) std::cerr << msg << std::endl;
#else
#define CERR_DEBUG(msg)
#endif

/* --- apb expansion tokens --- */
// TODO: more notes!
// expansion is top-level only, i.e. no expansion within expansion

// "@" is (currently) the selected character for token substitution by apb
// (implicitly " @", i.e. space-@).
static constexpr auto apb_expansion_prefix = '@';
using incr_else_errmsg_t = std::expected<size_t, std::string>;
// expansion functions take a view of the input string, a pointer
// to the position in that string at which the candidate expansion token appears,
// and a pointer to the output string to which the result of expansion is to be appended.
using apb_expand_fn_t =
    std::function<incr_else_errmsg_t (std::string_view, std::string*)>;
// clang-format off
#define DEF_EXPAND_LAMBDA \
  [] (std::string_view trailing, std::string* out) -> incr_else_errmsg_t
// clang-format on


static auto apb_keywords = []() {
  static auto kw_arr =
      std::to_array<std::pair<std::string_view, apb_expand_fn_t>> ({
          {"tag",
           DEF_EXPAND_LAMBDA {
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
                   fmt::format (
                       "expected single quotes, got {}", trailing[ic]
                   )
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
           }},
      });
  std::sort (
      begin (kw_arr), end (kw_arr), [] (const auto& a, const auto& b) {
        return a.first.size() > b.first.size();
      }
  );
  return kw_arr;
}();

// NOTE: if keywords are an argument to the function, token expansion becomes
// reusable with different keyword sets. Might be useful for limiting
// certain expansions to particular cmds.
// NOTE: token indicator char could be argument to function, but I don't have
// a use case for doing that as of now.
std::string apb_expand_tokens (std::string_view s, const token_opts_t opts)
{
  CERR_DEBUG (
      fmt::format ("called apb_expand_tokens on input string {}", s)
  );

  if (s.empty()) {
    return "";
  }
  // add 2 for " @"
  static auto shortest_match_len = apb_keywords.back().first.size() + 2;
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
        for (const auto& [kw, fn] : apb_keywords) {
          CERR_DEBUG (fmt::format ("Trying kw {}", kw));
          if (kw.size() > (s.size() - ic)) {
            continue;
          }
          if (std::strncmp (kw.data(), s.data() + ic, kw.size()) == 0) {
            // we have a match, call fn
            CERR_DEBUG (
                fmt::format (
                    "matched {}; calling expansion fn with ic at {}", kw,
                    ic
                )
            );
            if (const auto ret =
                    fn (std::string_view{s.data() + ic}, &out);
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
  SUBCASE (R"txt( @tag 'xx' is expanded to tags ->> '$.xx' )txt")
  {
    constexpr std::string_view expected =
        R"txt(WHERE tags ->> '$.RG' IS 'sample1';)txt";
    SUBCASE ("proper usage")
    {
      constexpr std::string_view input =
          R"txt(WHERE @tag 'RG' IS 'sample1';)txt";
      CHECK (apb_expand_tokens (input, {}) == expected);
    }

    SUBCASE ("whitespace irrelevant")
    {
      SUBCASE ("missing space")
      {
        constexpr std::string_view input =
            R"txt(WHERE @tag'RG' IS 'sample1';)txt";
        CHECK (apb_expand_tokens (input, {}) == expected);
      }

      SUBCASE ("extra spaces")
      {
        constexpr std::string_view input =
            R"txt(WHERE @tag   'RG' IS 'sample1';)txt";
        CHECK (apb_expand_tokens (input, {}) == expected);
      }
    }

    SUBCASE ("@ prefix mandatory")
    {
      constexpr std::string_view input =
          R"txt(WHERE tag 'RG' IS 'sample1';)txt";
      CHECK (apb_expand_tokens (input, {}) == input);
    }

    // FIXME: add misuse cases
  }
}

#endif  // DOCTEST_CONFIG_DISABLE
