#pragma once

#include "general.hpp"
#include "string.hpp"

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>


namespace core::utils {

// An enum described by a string::Enum_with_names specialization: the single source of the declared
// enumerators and of their count for every container keyed by an enum.
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

// Density makes this equivalent to "value is one of the declared enumerators": the declared values are
// unique and SIZE of them fit in [0, SIZE), so they are exactly 0..SIZE-1 and nothing else can land there.
// Hence the constraint - without density the range test would say nothing about being declared.
template< Enum_with_names_like Enum> requires (details::is_dense_enum< Enum>())
constexpr bool is_declared_enumerator(const Enum value) noexcept {
  const auto index = to_underlying(value);
  return !std::cmp_less(index, 0) && std::cmp_less(index, string::Enum_with_names< Enum>::SIZE);
}

} // namespace core::utils
