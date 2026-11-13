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

// expansion functions take a view of the input string, a pointer
// to the position in that string at which the token appears upon which
// expansion is to be attempted, and a pointer to the output string
// to which the result is to be appended.
// FIXME: How is failure to expand represented? Currently return
// is void, which is insufficient.
static auto apb_keywords = []() {
  using void_else_errmsg_t = std::expected<void, std::string>;
  static auto kw_arr = std::to_array<std::pair<
      std::string_view, std::function<void_else_errmsg_t (
                            std::string_view, size_t* pos, std::string*
                        )>>> ({
      {"tag",
       [] (std::string_view s, size_t* pos,
           std::string* out) -> void_else_errmsg_t {
         /* syntax: tag 'RG' IS... */
         auto fail = [] (std::string_view msg) {
           return std::unexpected<std::string> (
               fmt::format ("Failed to expand '@tag'- {}", msg)
           );
         };

         // jump keyword;
         *pos += 3;
         while (s[*pos] == ' ') {
           // NOTE: must be ++*pos or (*pos)++.
           // *pos++; does not work.
           ++*pos;
           if (*pos >= s.size()) {
             return fail ("expected single quotes, got end of string");
           }
         }
         if (s[*pos] != '\'') {
           return fail (
               fmt::format ("expected single quotes, got {}", s[*pos])
           );
         }
         ++*pos;
         std::string tagKey;
         while (s[*pos] != '\'') {
           // suboptimal. Could memcpy...
           tagKey += s[*pos];
           ++*pos;
           if (*pos >= s.size()) {
             return fail (
                 "expected matching ''' (single quote) after opening "
                 "quote, got end of string"
             );
           }
         }
         ++*pos;  // jump final single quote
         out->append (fmt::format ("tags ->> '$.{}'", tagKey));
         return {};
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
// "@" is (currently) the selected character for token substitution by apb
// (implicitly " @", i.e. space-@).
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
  // add two for " @"
  static auto shortest_match_len = apb_keywords.back().first.size() + 2;
  const auto slen = std::ssize (s);
  if (slen <= shortest_match_len) {
    CERR_DEBUG ("string too short to contain any keywords");
    // too short
    return std::string{s};
  }


  size_t ic = 0;
  std::string out;
  while (ic < s.size()) {
    switch (s[ic]) {
    case ' ': {
      CERR_DEBUG ("found ' ' (space)");
      out += ' ';
      ic++;
      if (s[ic] == '@') {
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
            if (const auto ret = fn (s, &ic, &out); !ret) {
              // FIXME: do something of more substance
              // on error
              CERR_DEBUG (
                  fmt::format ("expansion failed: {}", ret.error())
              );
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
    std::string input = R"txt(WHERE @tag 'RG' IS 'sample1';)txt";
    const auto output = apb_expand_tokens (input, {});
    CHECK (output == expected);
  }
}

#endif  // DOCTEST_CONFIG_DISABLE
