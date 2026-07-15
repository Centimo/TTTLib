#pragma once

#include "utils/general.hpp"

#include <concepts>
#include <condition_variable>
#include <functional>
#include <thread>
#include <type_traits>


namespace core {

namespace details {

// The executor optionally provides a non-static 'free()' method, called once after
// the thread joins. It must be a member function (the caller binds &T::free), so a
// static free() is deliberately rejected.
template< class T >
concept Has_free_method = requires(T& object) {
  { object.free() };
  requires std::is_member_function_pointer_v< decltype(&T::free) >;
};

} // namespace details

class Stop_flag {
 public:
  virtual ~Stop_flag() = default;

  void stop();
  [[nodiscard]] bool is_stopped() const;

 private:
  std::atomic< bool > _stop = false;
};

template<typename T>
concept Stoppable = std::derived_from<typename std::remove_cvref<T>::type, Stop_flag>;

class Thread final {
 public:
  template <Stoppable Executor> requires (!std::is_reference< Executor >::value)
  explicit Thread(
    Executor& executor
  )
    : _stop_function(std::bind(&Executor::stop, &executor))
    , _thread(std::make_unique<std::thread>(&Executor::execute, &executor))
  {
    if constexpr (details::Has_free_method< Executor >) {
      _free_function.emplace(std::bind(&Executor::free, &executor));
    }
  }

  Thread() = delete;
  Thread(const Thread&) = delete;

  ~Thread() {
    _stop_function();
    if (_thread->joinable()) {
      _thread->join();
    }

    if (_free_function) {
      (*_free_function)();
    }
  }

 private:
  std::function<void ()> _stop_function;
  std::optional< std::function< void () > > _free_function = std::nullopt;
  std::unique_ptr<std::thread> _thread;
};

class Notifier {
 public:
  Notifier() = default;
  Notifier(const Notifier&) = delete;
  Notifier(Notifier&&) = delete;

  ~Notifier();

 public:
  void wait();
  void notify();

 private:
  std::condition_variable _condition_variable;
  bool _condition = false;

  mutable std::mutex _synchronization;
};

} // namespace Core