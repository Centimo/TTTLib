#include <mutex>
#include "Thread.hpp"


namespace core {

void Stop_flag::stop() {
  _stop.store(true);
}

bool Stop_flag::is_stopped() const {
  return _stop.load();
}


void Notifier::wait() {
  std::unique_lock lock(_synchronization);
  _condition_variable.wait(lock, [this]{ return _condition; });
  _condition = false;
}

void Notifier::notify() {
  std::unique_lock lock(_synchronization);
  _condition = true;
  _condition_variable.notify_one();
}

Notifier::~Notifier() {
  notify();
}

} // namespace Core