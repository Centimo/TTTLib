#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <string_view>
#include <string>
#include <system_error>
#include <type_traits>
#include <tuple>


namespace core::utils {

template <typename E>
constexpr typename std::underlying_type<E>::type to_underlying(E e) noexcept {
    return static_cast<typename std::underlying_type<E>::type>(e);
}

template <class T>
class Optional_reference : public std::optional<std::reference_wrapper<T>> {
 public:
  using value_type = T;
  Optional_reference() = default;
  Optional_reference(T& value) : std::optional<std::reference_wrapper<T>>(value) {};
  constexpr operator bool() const noexcept { return this->has_value(); };

  T* operator->() const noexcept {
    return &(this->value().get());
  }

  T& operator * () const noexcept {
    return this->value().get();
  }
};

class Atomic_flag : public std::atomic< bool > {
 public:
  using std::atomic< bool >::atomic;
  bool compare_exchange_expect_false();
  bool compare_exchange_expect_true();
};

using Index_range = std::ranges::iota_view< size_t, size_t >;
Index_range index_range(size_t range_size);

template< class T >
class Shared_reference {
 public:
  template< class... Arguments >
  explicit Shared_reference(Arguments&&... arguments) requires std::is_constructible_v< T, Arguments... >
    : _pointer(std::make_shared< T >(std::forward<Arguments>(arguments)...))
  {}

  Shared_reference() requires std::is_default_constructible_v< T > : _pointer(std::make_shared< T >()) {}
  Shared_reference(const Shared_reference& copy) : _pointer(copy._pointer) {}
  Shared_reference(Shared_reference&&) = delete;
  Shared_reference(const Shared_reference&& copy) noexcept : _pointer(copy._pointer) {}

  void swap(Shared_reference& other) noexcept {
    _pointer.swap(other._pointer);
  }

  template< class... Arguments >
  void reset(Arguments&&... arguments) requires std::is_constructible_v< T, Arguments... > {
    _pointer = std::make_shared< T >(std::forward<Arguments>(arguments)...);
  }

  void reset(T&& new_value) requires std::is_constructible_v< T, decltype(std::forward< T >(new_value)) > {
    _pointer = std::make_shared< T >(std::forward< T >(new_value));
  }

  void reset(const T& new_value) requires std::is_copy_constructible_v< T > {
    _pointer = std::make_shared< T >(new_value);
  }

  static std::optional< Shared_reference > make(const std::shared_ptr< T >& pointer) {
    if (!pointer) {
      return std::nullopt;
    }

    const auto& result = Shared_reference(pointer);
    return result;
  }

  static std::optional< Shared_reference > make(std::shared_ptr< T >&& pointer) {
    if (!pointer) {
      return std::nullopt;
    }

    const auto& result = Shared_reference(pointer);
    return result;
  }

  static std::optional< Shared_reference > make(T* pointer) {
    return make(std::shared_ptr< T >(pointer));
  }

  Shared_reference& operator = (const Shared_reference& copy)  = default;
  Shared_reference& operator = (Shared_reference&& copy) = delete;

  Shared_reference& operator = (Shared_reference copy) noexcept {
    _pointer = copy._pointer;
    return *this;
  }

  explicit operator T& () {
    return *_pointer;
  }

  T* operator->() const noexcept {
    return _pointer.get();
  }

  T& operator * () const noexcept {
    return *_pointer;
  }

  std::shared_ptr< T > get_shared_pointer() const {
    return _pointer;
  }

 private:
  explicit Shared_reference(std::shared_ptr< T >&& pointer) : _pointer(pointer) {}
  explicit Shared_reference(const std::shared_ptr< T >& pointer) : _pointer(pointer) {}

 private:
  std::shared_ptr< T > _pointer;
};

template< class T >
using Outcome_shared_reference = std::expected< core::utils::Shared_reference< T >, std::error_code >;

/*
template< class T >
struct Unique_reference {
  template< class... Arguments >
  explicit Unique_reference(Arguments&&... arguments) requires std::is_constructible_v< T, Arguments... >
    : _pointer(std::make_unique< T >(std::forward<Arguments>(arguments)...))
  {}

  Unique_reference() requires std::is_default_constructible_v< T > : _pointer(std::make_unique< T >()) {}
  Unique_reference(const Unique_reference&) = delete;
  Unique_reference(Unique_reference&&) = delete;
  Unique_reference(const Unique_reference&& temporary) : _pointer(std::move(temporary._pointer)) {
    if (!_pointer) {
      throw std::runtime_error("Unique_reference: copy from null temporary unique_reference");
    }
  }

  static Unique_reference make_or_throw(std::unique_ptr< T >&& pointer) {
    if (!pointer) {
      throw std::runtime_error("Unique_reference::make_or_throw: null unique_ptr");
    }

    return Unique_reference(std::move(pointer));
  }

  static std::optional< Unique_reference > make(T* pointer) {
    return make(std::unique_ptr< T >(pointer));
  }

  explicit operator T& () {
    return *_pointer;
  }

  T* operator->() const noexcept {
    return _pointer.get();
  }

  std::unique_ptr< T > get_unique_pointer() const {
    return _pointer;
  }

 private:
  explicit Unique_reference(std::unique_ptr< T >&& pointer) : _pointer(pointer) {}

 private:
  mutable std::unique_ptr< T > _pointer;
};

template< class T >
using Outcome_unique_reference = cvs::common::CVSOutcome< core::utils::Unique_reference< T > >;
*/

template<typename... Ranges>
auto combine(Ranges&&... rngs) {
  return std::views::zip(std::forward<Ranges>(rngs)...);
}

} // namespace core::utils