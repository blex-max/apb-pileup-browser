#pragma once

#include "plog/Log.h"
#include <cassert>
#include <format>
#include <stdexcept>
#include <type_traits>

namespace singleton {

// base class from which
// singletons may inherit
struct Singleton {
    bool initialised = false;

    // no copies or moves
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton(Singleton&&) = delete;
    Singleton& operator=(Singleton&&) = delete;

    protected:
    // prevent raw construction
    Singleton () = default;
    ~Singleton () = default;
};

template <typename ChildClassT> 
constexpr bool IsSingletonT = std::is_base_of_v<Singleton, ChildClassT>;

namespace singleton_internal {
template <typename S>
requires IsSingletonT<S>
inline S& create () {
    static S ctx;
    return ctx;
}
} // end namespace

template <typename S>
requires IsSingletonT<S>
inline void init () {
    auto& ctx = singleton_internal::create<S>();
    ctx.initialised = true;
    PLOGD << std::format ("Initialised singleton of type {}", typeid(S).name());
}

template <typename S>
requires IsSingletonT<S>
inline S& get () {
    auto& ctx = singleton_internal::create<S>();
    if (!ctx.initialised) {
        throw std::runtime_error (
            std::format (
                "Retrieval of singleton {} before initialisation",
                typeid(S).name()
            )
        );
    }
    return ctx;
}

}   // end namespace
