#pragma once

#include "With_names.hpp"

#include "../general.hpp"
#include "../meta.hpp"

#include <cstddef>
#include <tuple>
#include <utility>


namespace core::utils::enums {

namespace details {

// Uniqueness of VALUES is already guaranteed by With_names_base (Sort_by_value duplicate check),
// so range [0, SIZE) plus uniqueness implies VALUES is a permutation of 0..SIZE-1, i.e. a dense enum.
template< With_names_like Enum>
consteval bool is_dense_enum() {
  using Names = With_names< Enum>;
  for (const Enum value : Names::VALUES) {
    const auto underlying_value = to_underlying(value);
    if (std::cmp_less(underlying_value, 0) || std::cmp_greater_equal(underlying_value, Names::SIZE)) {
      return false;
    }
  }

  return true;
}

} // namespace details

// Density makes this equivalent to "value is one of the declared enumerators": the declared values are
// unique and SIZE of them fit in [0, SIZE), so they are exactly 0..SIZE-1 and nothing else can land there.
// Hence the constraint - without density the range test would say nothing about being declared.
template< With_names_like Enum> requires (details::is_dense_enum< Enum>())
constexpr bool is_declared_enumerator(const Enum value) noexcept {
  const auto index = to_underlying(value);
  return !std::cmp_less(index, 0) && std::cmp_less(index, With_names< Enum>::SIZE);
}

namespace details {

// Maps a key onto the type standing at its position in Types, for every container that lays one type per
// enumerator. A struct rather than a constrained alias: it can carry the static_assert, so a key that is
// not a declared enumerator is reported as such instead of as std::tuple_element's own out-of-range
// failure. The valid case is a constrained specialization, not a static_assert next to the alias: an index
// outside the list would otherwise still be formed after the assertion had fired, adding that very failure
// back on top.
template< class Types, With_names_like Enum, Enum key>
struct Element {
  static_assert(
    meta::ALWAYS_FALSE< Types>,
    "Enum value is not declared in the With_names specialization"
  );
};

template< class Types, With_names_like Enum, Enum key> requires (is_declared_enumerator(key))
struct Element< Types, Enum, key> {
  using type = std::tuple_element_t< static_cast< std::size_t>(to_underlying(key)), Types>;
};

} // namespace details

} // namespace core::utils::enums
