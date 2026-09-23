#pragma once

#include <concepts>
#include <utility>

// abseil style scope guard. runs `callback` on scope exit,
// unless cancelled or already invoked early.
template <std::invocable Callback>
class [[nodiscard]] Cleanup {
 public:
  explicit Cleanup (Callback fn) : callback (std::move (fn)) {}

  Cleanup (const Cleanup&) = delete;
  Cleanup (Cleanup&&) = delete;
  Cleanup& operator= (const Cleanup&) = delete;
  Cleanup& operator= (Cleanup&&) = delete;

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

  ~Cleanup() { invoke(); }

 private:
  Callback callback;
  bool armed = true;
};

template <typename Callback>
Cleanup (Callback) -> Cleanup<Callback>;
