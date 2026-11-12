#include <string>


struct token_opts_t {};
std::string apb_expand_tokens (
    std::string_view s, const token_opts_t opts
);
