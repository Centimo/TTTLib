#pragma once

// It's rework of trantor::MpscQueue

#include <atomic>
#include <type_traits>
#include <memory>
#include <cassert>
#include <optional>


namespace core {

// maximal_size = 0 for unlimited
template< typename T, size_t maximal_size = 100 >
class MPSC_queue {
 public:
  MPSC_queue()
    : _head(new BufferNode)
    , _tail(_head.load(std::memory_order_relaxed))
  {}

  MPSC_queue(const MPSC_queue&) = delete;
  MPSC_queue& operator=(const MPSC_queue&) = delete;

  ~MPSC_queue() {
    while (this->dequeue_flash()) {}

    BufferNode* front = _head.load(std::memory_order_relaxed);
    delete front;
  }

  template< class... Arguments >
  bool enqueue(Arguments&& ... arguments) requires std::is_constructible_v< T, Arguments... > {
    if constexpr (maximal_size > 0) {
      if (_counter.load() > maximal_size) {
        return false;
      }
    }

    BufferNode* new_head {new BufferNode(std::forward< Arguments >(arguments)...)};
    ++_counter;
    BufferNode* previous_head {_head.exchange(new_head, std::memory_order_acq_rel)};
    previous_head->_next.store(new_head, std::memory_order_release);
    return true;
  }

  bool dequeue(T& output) {
    BufferNode* current_tail = _tail.load(std::memory_order_relaxed);
    BufferNode* next = current_tail->_next.load(std::memory_order_acquire);

    if (next == nullptr) {
      return false;
    }

    output = std::move(*(next->_data_pointer));
    remove_current_tail(current_tail, next);
    return true;
  }

  std::optional<T> dequeue()
    requires
      std::is_move_constructible<T>::value
      || std::is_copy_constructible<T>::value
  {
    BufferNode* current_tail = _tail.load(std::memory_order_relaxed);
    BufferNode* next = current_tail->_next.load(std::memory_order_acquire);

    if (next == nullptr) {
      return {};
    }

    std::optional< T> result;
    if constexpr (std::is_move_constructible< T >::value) {
      result.template emplace(std::move(*(next->_data_pointer)));
    }
    else {
      result.template emplace(*(next->_data_pointer));
    }

    remove_current_tail(current_tail, next);
    return result;
  }

  std::unique_ptr< T> dequeue_unique_ptr() {
    BufferNode* current_tail = _tail.load(std::memory_order_relaxed);
    BufferNode* next = current_tail->_next.load(std::memory_order_acquire);

    if (next == nullptr) {
      return {};
    }

    auto result = std::unique_ptr< T >(next->_data_pointer);
    remove_current_tail(current_tail, next, false);
    return std::move(result);
  }

  bool empty() const {
    BufferNode* tail = _tail.load(std::memory_order_relaxed);
    BufferNode* next = tail->_next.load(std::memory_order_acquire);
    return next == nullptr;
  }

  [[nodiscard]] size_t size() const {
    return _counter.load();
  }

 private:
  struct BufferNode {
    BufferNode() = default;

    explicit BufferNode(const T& data) : _data_pointer(new T(data)) {}
    explicit BufferNode(T&& data) requires std::is_move_constructible_v< T > : _data_pointer(new T(std::move(data))) {}
    explicit BufferNode(const T&& data) : _data_pointer(new T(std::move(data))) {}

    template< class... Arguments >
    explicit BufferNode(Arguments&& ... arguments) : _data_pointer(new T(std::forward< Arguments >(arguments)...)) {}

    T* _data_pointer {nullptr};
    std::atomic< BufferNode* > _next {nullptr};
  };

 private:
  bool dequeue_flash() {
    BufferNode* current_tail = _tail.load(std::memory_order_relaxed);
    BufferNode* next = current_tail->_next.load(std::memory_order_acquire);

    if (next == nullptr) {
      return false;
    }

    remove_current_tail(current_tail, next);
    return true;
  }

  void remove_current_tail(BufferNode* current_tail, BufferNode* next, bool delete_next_data = true) {
    if (delete_next_data) {
      delete next->_data_pointer;
    }
    _tail.store(next, std::memory_order_release);
    --_counter;
    delete current_tail;
  }

  std::atomic< BufferNode*> _head;
  std::atomic< BufferNode*> _tail;
  std::atomic< size_t> _counter;
};

}  // namespace core
