#pragma once

#include <atomic>
#include <chrono>
#include <compare>
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
#include <utility>


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

template< class T>
  requires std::is_object_v< T> && std::is_nothrow_move_constructible_v< std::remove_const_t< T>>
class Swappable {
  using Value = std::remove_const_t< T>;
  union { Value _value; };   // a union decouples the member's lifetime so it can be rebuilt in place

  // Every read goes through here. After destroy_at + construct_at reuse the storage, the member name
  // '_value' no longer designates the new object when Value has a const or reference member (which is the
  // very case this wrapper exists for) — accessing it directly would be undefined. std::launder yields a
  // pointer to the object actually living there. Construction sites still name '_value' directly: there
  // it is the initialization target, not a read of a reused slot.
  constexpr Value& value() noexcept {
    return *std::launder(std::addressof(_value));
  }

  constexpr const Value& value() const noexcept {
    return *std::launder(std::addressof(_value));
  }

 public:
  template< class... Arguments>
    requires std::constructible_from< Value, Arguments...>
  constexpr explicit Swappable(Arguments&&... arguments)
    noexcept(std::is_nothrow_constructible_v< Value, Arguments...>)
    : _value(std::forward< Arguments>(arguments)...)
  {}

  constexpr Swappable(const Swappable& other) requires std::is_copy_constructible_v< Value>
    : _value(other.value())
  {}

  constexpr Swappable(Swappable&& other) noexcept
    : _value(std::move(other.value()))
  {}

  constexpr Swappable& operator = (const Swappable& other) requires std::is_copy_constructible_v< Value> {
    if (this == std::addressof(other)) [[unlikely]] {
      return *this;
    }

    Value copy(other.value());                                   // may throw; slot stays intact
    std::destroy_at(std::addressof(_value));
    std::construct_at(std::addressof(_value), std::move(copy));   // nothrow (constrained on the class)
    return *this;
  }

  constexpr Swappable& operator = (Swappable&& other) noexcept {
    if (this == std::addressof(other)) [[unlikely]] {
      return *this;
    }

    std::destroy_at(std::addressof(_value));
    std::construct_at(std::addressof(_value), std::move(other.value()));
    return *this;
  }

  // Replace the wrapped object with a fresh value. The slot is replaceable even when T is const —
  // that is the whole point — so these are available regardless of T's constness.
  constexpr Swappable& operator = (const Value& new_value) requires std::is_copy_constructible_v< Value> {
    Value copy(new_value);                                       // may throw; slot stays intact
    std::destroy_at(std::addressof(_value));
    std::construct_at(std::addressof(_value), std::move(copy));   // nothrow (constrained on the class)
    return *this;
  }

  constexpr Swappable& operator = (Value&& new_value) noexcept {
    std::destroy_at(std::addressof(_value));
    std::construct_at(std::addressof(_value), std::move(new_value));
    return *this;
  }

  constexpr ~Swappable() {
    std::destroy_at(std::addressof(value()));
  }

  constexpr T& operator * () noexcept {
    return value();
  }

  constexpr const T& operator * () const noexcept {
    return value();
  }

  constexpr T* operator -> () noexcept {
    return std::addressof(value());
  }

  constexpr const T* operator -> () const noexcept {
    return std::addressof(value());
  }

  // Compare the wrapped values. Written out (not '= default') because a defaulted comparison is deleted
  // for a class with a union member; the constraints make each operator exist only when Value supports it.
  constexpr bool operator == (const Swappable& other) const
    noexcept(noexcept(std::declval< const Value&>() == std::declval< const Value&>()))
    requires std::equality_comparable< Value>
  {
    return value() == other.value();
  }

  constexpr auto operator <=> (const Swappable& other) const
    noexcept(noexcept(std::declval< const Value&>() <=> std::declval< const Value&>()))
    requires std::three_way_comparable< Value>
  {
    return value() <=> other.value();
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

} // namespace core::utils