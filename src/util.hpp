#include <iostream>

template<typename... Args>
constexpr void log_err(std::format_string<Args...> fmt, Args&&... args) {
    std::cerr << std::format(fmt, std::forward<Args>(args)...) << std::endl;
}

