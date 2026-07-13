#pragma once

#include <cassert>
#include <format>
#include <stdexcept>
#include <type_traits>

#include "plog/Log.h"

namespace ctx {

// base class from which
// singletons may inherit
struct Context {
 protected:
    // prevent raw construction
  Context() = default;
  ~Context() = default;

 private:
  bool initialised = false;
  friend void set_init(Context& s);
  friend bool check_init(Context& s);

 public:
    // no copies or moves
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  Context(Context&&) = delete;
  Context& operator=(Context&&) = delete;
};
inline void set_init(Context& s) { s.initialised = true; }
inline bool check_init(Context& s) { return s.initialised; }

template <typename ChildClassT>
constexpr bool IsContextT =
    std::is_base_of_v<Context, ChildClassT>;

namespace singleton_internal {
template <typename S>
  requires IsContextT<S>
inline S& create()
{
  static S ctx;
  return ctx;
}
} // namespace singleton_internal

template <typename S>
  requires IsContextT<S>
inline void init()
{
  auto& s = singleton_internal::create<S>();
  set_init(s);
  PLOGD << std::format(
      "Initialised singleton of type {}", typeid(S).name()
  );
}

template <typename S>
  requires IsContextT<S>
inline S& get()
{
  auto& s = singleton_internal::create<S>();
  if (!check_init(s)) {
    throw std::runtime_error(
        std::format(
            "Retrieval of singleton {} "
            "before initialisation",
            typeid(S).name()
        )
    );
  }
  return s;
}

}   // namespace ctx
