#pragma once

#include <concepts>
#include <utility>

// abseil style scope guard. runs `callback` on scope exit,
// unless cancelled or already invoked early.
template <std::invocable Callback>
class [[nodiscard]] Defer {
 public:
  explicit Defer (Callback fn) : callback (std::move (fn)) {}

  Defer (const Defer&) = delete;
  Defer (Defer&&) = delete;
  Defer& operator= (const Defer&) = delete;
  Defer& operator= (Defer&&) = delete;

  // Disarm without running the callback.
  void cancel() { armed = false; }

  // Run the callback now instead of at scope exit.
  void invoke()
  {
    if (armed) {
      armed = false;
      callback();
    }
  }

  ~Defer() { invoke(); }

 private:
  Callback callback;
  bool armed = true;
};

template <typename Callback>
Defer (Callback) -> Defer<Callback>;
