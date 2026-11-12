#include "expand.hpp"

#include <fmt/format.h>

#include <array>
#include <cstring>
#include <functional>
#include <map>
#include <string_view>

#ifdef STDERR_DEBUG
#include <iostream>
#define ERR_PRINT(msg) std::cerr << msg << std::endl;
#else
#define ERR_PRINT(msg)
#endif

// expansion functions take a view of the string and a pointer
// to the position at which the token appears upon which
// expansion is to be attempted.
// They return a string view, to be concatenated into the output string.
// FIXME: How is failure to expand represented? Is empty output +
// pointer not moved sufficient?
static auto apb_keywords = []() {
  static auto kw_arr = std::to_array<std::pair<
      std::string_view,
      std::function<std::string_view (std::string_view, size_t* pos)>>> (
      {{"test", [] (std::string_view, size_t* pos) {
          constexpr std::string_view replace = "hello";
          *pos += 4;
          return replace;
        }}}
  );
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
  ERR_PRINT (
      fmt::format ("called apb_expand_tokens on input string {}", s)
  );

  if (s.empty()) {
    return "";
  }
  // add two for " @"
  static auto shortest_match_len = apb_keywords.back().first.size() + 2;
  const auto slen = std::ssize (s);
  if (slen <= shortest_match_len) {
    ERR_PRINT ("string too short to contain any keywords");
    // too short
    return std::string{s};
  }


  size_t ic = 0;
  std::string out;
  while (ic < s.size()) {
    switch (s[ic]) {
    case ' ': {
      ERR_PRINT ("found ' ' (space)");
      out += ' ';
      ic++;
      if (s[ic] == '@') {
        ERR_PRINT ("found @ after space");
        ic++;
        // we may have found an apb token.
        // Does a known apb keyword appear after it?
        ERR_PRINT (fmt::format ("trailing string is: {}", s.substr (ic)));
        for (const auto& [kw, fn] : apb_keywords) {
          ERR_PRINT (fmt::format ("Trying kw {}", kw));
          if (kw.size() > (s.size() - ic)) {
            continue;
          }
          if (std::strncmp (kw.data(), s.data() + ic, kw.size()) == 0) {
            // we have a match, call fn
            ERR_PRINT (
                fmt::format (
                    "matched {}; calling expansion fn with ic at {}", kw,
                    ic
                )
            );
            out += fn (s, &ic);
            ERR_PRINT (
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
    ERR_PRINT (
        fmt::format ("at end of cycle, expanded string is {}", out)
    );
  }

  return out;
}

#ifndef DOCTEST_CONFIG_DISABLE

#include "doctest.h"

TEST_CASE ("apb_expand_tokens")
{
  SUBCASE ("' @flag' is expanded/swapped to ' hello'")
  {
    std::string testS = " @test world";
    const auto expandedStr = apb_expand_tokens (testS, {});
    REQUIRE (expandedStr == " hello world");
  }
}

#endif  // DOCTEST_CONFIG_DISABLE
