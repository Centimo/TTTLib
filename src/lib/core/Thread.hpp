#pragma once

#include "utils/general.hpp"

#include <cvs/common/general.hpp>

#include <concepts>
#include <condition_variable>
#include <functional>
#include <thread>


namespace core {

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
  template< class T >
  class HasInstanceMethod {
    CVS_HAS_INSTANCE_METHOD_DEFAULT(free);
  };

  template <Stoppable Executor> requires (!std::is_reference< Executor >::value)
  explicit Thread(
    Executor& executor
  )
    : _stop_function(std::bind(&Executor::stop, &executor))
    , _thread(std::make_unique<std::thread>(&Executor::execute, &executor))
  {
    if constexpr (HasInstanceMethod< Executor >::template free_v<>) {
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