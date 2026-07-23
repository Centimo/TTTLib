#pragma once

#include "general.hpp"
#include "string.hpp"

#include <array>
#include <compare>
#include <concepts>
#include <stdexcept>
#include <type_traits>
#include <utility>


namespace core::utils {

template< class Enum>
concept Enum_with_names_like =
  std::is_enum_v< Enum> &&
  requires {
    { string::Enum_with_names< Enum>::SIZE } -> std::convertible_to< std::size_t>;
    { string::Enum_with_names< Enum>::VALUES[0] } -> std::convertible_to< Enum>;
  };

namespace details {

// Uniqueness of VALUES is already guaranteed by Enum_with_names_base (Sort_by_value duplicate check),
// so range [0, SIZE) plus uniqueness implies VALUES is a permutation of 0..SIZE-1, i.e. a dense enum.
template< Enum_with_names_like Enum>
consteval bool is_dense_enum() {
  using Names = string::Enum_with_names< Enum>;
  for (const Enum value : Names::VALUES) {
    const auto underlying_value = to_underlying(value);
    if (std::cmp_less(underlying_value, 0) || std::cmp_greater_equal(underlying_value, Names::SIZE)) {
      return false;
    }
  }

  return true;
}

} // namespace details

template< Enum_with_names_like Enum, class T>
class Enum_array {
  using Names = string::Enum_with_names< Enum>;
  static_assert(
    details::is_dense_enum< Enum>(),
    "Enum_array requires a dense enum: values must form a permutation of 0..SIZE-1"
  );

  std::array< T, Names::SIZE> _data {};

  // Density makes this equivalent to "key is one of the declared enumerators": the declared values are
  // unique and SIZE of them fit in [0, SIZE), so they are exactly 0..SIZE-1 and nothing else can land there.
  static constexpr bool is_valid_key(const Enum key) noexcept {
    const auto index = to_underlying(key);
    return !std::cmp_less(index, 0) && std::cmp_less(index, Names::SIZE);
  }

 public:
  static constexpr std::size_t SIZE = Names::SIZE;

  constexpr Enum_array() = default;

  // The 'sizeof...(Args) > 1' guard keeps this template from hijacking the copy/move constructor
  // when SIZE == 1 and T has a greedy converting constructor (std::any, std::variant, ...).
  // Only the single-element form is explicit: there it guards against an unintended T-to-Enum_array
  // conversion, whereas a multi-element list ({a, b, c}) is unambiguous and stays brace-initializable
  // like std::array, so it can appear in aggregates and copy-list-initialization.
  template< class... Args>
    requires
      (sizeof...(Args) == SIZE)
      && (sizeof...(Args) > 1 || !(std::same_as< Enum_array, std::remove_cvref_t< Args>> && ...))
  explicit(SIZE == 1) constexpr Enum_array(Args&&... args) : _data { std::forward< Args>(args)... } {}

  // Key known at compile time: validity is proven, so the access needs no check and cannot throw.
  template< Enum key>
  constexpr T& at() noexcept {
    static_assert(is_valid_key(key), "Enum value is not declared in the Enum_with_names specialization");
    return _data[to_underlying(key)];
  }

  template< Enum key>
  constexpr const T& at() const noexcept {
    static_assert(is_valid_key(key), "Enum value is not declared in the Enum_with_names specialization");
    return _data[to_underlying(key)];
  }

  // Checked access for a runtime key, mirroring std::array::at. A runtime key cannot be proven valid: it may
  // be cast from an arbitrary integer, or be an enumerator added to the enum but never declared in
  // Enum_with_names.
  constexpr T& at(const Enum key) {
    if (!is_valid_key(key)) {
      throw std::out_of_range("Enum_array: enum value is not a declared enumerator");
    }

    return _data[to_underlying(key)];
  }

  constexpr const T& at(const Enum key) const {
    if (!is_valid_key(key)) {
      throw std::out_of_range("Enum_array: enum value is not a declared enumerator");
    }

    return _data[to_underlying(key)];
  }

  // Unchecked access, mirroring std::array::operator[]: passing a key that is not a declared enumerator is
  // undefined behaviour. Use at() when the key cannot be trusted.
  constexpr T& operator[](const Enum key) noexcept {
    return _data[to_underlying(key)];
  }

  constexpr const T& operator[](const Enum key) const noexcept {
    return _data[to_underlying(key)];
  }

  // Forward to the underlying array. Written as ordinary members (not '= default') so their bodies
  // instantiate lazily, only where a comparison is actually used: for an element type that cannot be
  // compared the operators simply never come into existence, whereas a defaulted comparison is
  // instantiated together with the class and would hard-error inside std::array's own comparison.
  constexpr bool operator == (const Enum_array& other) const {
    return _data == other._data;
  }

  constexpr auto operator <=> (const Enum_array& other) const {
    return _data <=> other._data;
  }

  constexpr auto begin() noexcept {
    return _data.begin();
  }

  constexpr auto begin() const noexcept {
    return _data.begin();
  }

  constexpr auto end() noexcept {
    return _data.end();
  }

  constexpr auto end() const noexcept {
    return _data.end();
  }
};

} // namespace core::utils
