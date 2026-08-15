#pragma once

#include "general.hpp"
#include "string.hpp"

#include <array>
#include <compare>
#include <concepts>
#include <initializer_list>
#include <ranges>
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

  // Private tag: keeps the delegating constructor below reachable only through the from_range_t
  // constructor, so it can never enter overload resolution for an external call.
  struct Range_fill {};

  // Stands in for "T swallows anything": a type nobody can name, so only a greedy converting constructor
  // (std::any and the like) accepts it. Asking whether T is constructible from Enum_array itself would
  // make the constraint depend on itself — that question re-enters this class's own overload set.
  struct Greediness_probe {};

  // Consumes one element from the range, in order. The unused index only expands the pack SIZE times.
  // Parenthesized initialization is the same form the variadic constructor uses, so an element is built
  // identically through either; read-then-increment stays within what input_iterator guarantees, unlike
  // '*it++'.
  template< class Iterator, class Sentinel>
  static constexpr T take_next(Iterator& first, const Sentinel last, const std::size_t /* index */) {
    if (first == last) {
      throw std::out_of_range("Enum_array: range has fewer elements than the enum has values");
    }

    T value(*first);
    ++first;
    return value;
  }

  // Braced-init evaluates its elements left to right, so the shared iterator advances in order across the
  // pack. Private: reached only through the from_range_t constructor's delegation.
  template< class Iterator, class Sentinel, std::size_t... indexes>
  constexpr Enum_array(Range_fill, Iterator first, const Sentinel last, std::index_sequence< indexes...>)
    : _data { take_next(first, last, indexes)... }
  {
    if (first != last) {
      throw std::out_of_range("Enum_array: range has more elements than the enum has values");
    }
  }

 public:
  static constexpr std::size_t SIZE = Names::SIZE;

  constexpr Enum_array() = default;

  // The 'sizeof...(Args) > 1' guard keeps this template from hijacking the copy/move constructor
  // when SIZE == 1 and T has a greedy converting constructor (std::any, std::variant, ...).
  // Only the single-element form is explicit: there it guards against an unintended T-to-Enum_array
  // conversion. A braced list reaches the initializer_list constructor below instead, except for the
  // element types excluded there, which fall back here.
  //
  // Each argument builds its element in place, with no intermediate T to copy or move: the parenthesized
  // form is a prvalue initializing the array slot directly, and it accepts an explicit constructor, which
  // copy-initializing the member from the pack would reject.
  //
  // The two halves of the constraint differ on purpose. 'T(argument)' is what the member initializer does
  // and matches take_next, so an element type is built the same way here and through from_range —
  // std::vector(3) stays a vector of three, rather than the one-element list 'T{3}' would produce.
  // 'T{argument}' is required in addition purely for its narrowing rules, which the parenthesized form
  // does not have: without it a double argument would silently truncate into an int element.
  template< class... Args>
    requires
      (sizeof...(Args) == SIZE)
      && (sizeof...(Args) > 1 || !(std::same_as< Enum_array, std::remove_cvref_t< Args>> && ...))
      && (requires (Args&& argument) {
            T(std::forward< Args>(argument));
            T{std::forward< Args>(argument)};
          } && ...)
  explicit(SIZE == 1) constexpr Enum_array(Args&&... args) : _data { T(std::forward< Args>(args))... } {}

  // In list-initialization an initializer_list constructor is considered before every other candidate, so
  // this is what {a, b, c} resolves to, and the count it carries is a run-time property: a braced list of
  // the wrong length throws instead of failing to compile, unlike the variadic constructor above.
  //
  // Two element types are excluded so that they keep reaching that variadic constructor. A move-only T
  // cannot come from an initializer_list at all — its elements are const. A T that swallows anything
  // (std::any and the like) would turn 'Enum_array copy{source}' into a one-element list wrapping the
  // source, which is the very hijack the self-exclusion above prevents; being considered first, this
  // constructor would otherwise bypass it.
  //
  // Note that elements are copied even from an rvalue: {std::move(a), ...} copies, where the variadic
  // constructor moves.
  constexpr Enum_array(const std::initializer_list< T> values)
    requires std::copy_constructible< T> && (!std::constructible_from< T, Greediness_probe>)
    : Enum_array(std::from_range, values)
  {}

  // Fill positionally from a range: element i goes to the enumerator with underlying value i. The range
  // must yield exactly SIZE elements — too few or too many throws, so the array is never left partial.
  template< std::ranges::input_range Range>
    requires std::constructible_from< T, std::ranges::range_reference_t< Range>>
  constexpr Enum_array(std::from_range_t, Range&& range)
    : Enum_array(Range_fill{}, std::ranges::begin(range), std::ranges::end(range), std::make_index_sequence< SIZE>{})
  {}

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
