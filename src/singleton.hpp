#pragma once

#include <type_traits>

namespace singleton {

struct Singleton {
    // no copies or moves
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
    Singleton(Singleton&&) = delete;
    Singleton& operator=(Singleton&&) = delete;

    protected:
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
    singleton_internal::create<S>();
}

template <typename S>
requires IsSingletonT<S>
inline S& get () {
    return singleton_internal::create<S>();
}

}   // end namespace
