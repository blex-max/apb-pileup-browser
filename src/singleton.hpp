#pragma once

#include <cassert>
#include <format>
#include <stdexcept>
#include <type_traits>

#include "plog/Log.h"

namespace singleton {

// base class from which
// singletons may inherit
struct Singleton {
    protected:
    // prevent raw construction
    Singleton () = default;
    ~Singleton () = default;

    private:
    bool initialised = false;
    friend void set_init (Singleton& s);
    friend bool check_init (Singleton& s);

    public:
    // no copies or moves
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton(Singleton&&) = delete;
    Singleton& operator=(Singleton&&) = delete;
};
inline void set_init (Singleton& s) {
    s.initialised = true;
}
inline bool check_init (Singleton& s) {
    return s.initialised;
}

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
    auto& s = singleton_internal::create<S>();
    set_init(s);
    PLOGD << std::format ("Initialised singleton of type {}", typeid(S).name());
}

template <typename S>
requires IsSingletonT<S>
inline S& get () {
    auto& s = singleton_internal::create<S>();
    if (!check_init(s)) {
        throw std::runtime_error (
            std::format (
                "Retrieval of singleton {} before initialisation",
                typeid(S).name()
            )
        );
    }
    return s;
}

}   // end namespace
