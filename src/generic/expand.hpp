#pragma once

#include <expected>
#include <span>
#include <string>
#include <string_view>


using incr_else_errmsg_t = std::expected<size_t, std::string>;

// expansion functions take a view of the input from the matched token
// onwards, and a pointer to the output string to append the expansion to.
// Return number of chars consumed, else an error message.
using apb_expand_fn_t =
    incr_else_errmsg_t (std::string_view, std::string*);

// expansion tokens take arguments and transform
// them into a dynamic snippet.
struct apb_expansion_token_t {
  std::string_view token;
  apb_expand_fn_t* fn;
};

// substitution tokens directly replace
// their token with a fixed snippet.
// They do not take arguments.
struct apb_substitution_token_t {
  // NOTE: these will probably
  // end up needing to be strings
  // rather than string views
  std::string_view token;
  std::string_view replacement;
};

struct token_opts_t {
  // hypothetical options...
};
// Apply token expansion to s, returning the expanded string.
// NOTE: keywords are an argument to the function, so token expansion becomes
// reusable with different keyword sets.
std::string apb_expand_tokens (
    std::string_view s,
    std::span<const apb_expansion_token_t*> expansionRegistry,
    std::span<const apb_substitution_token_t*> substitutionRegistry,
    const token_opts_t opts
);
