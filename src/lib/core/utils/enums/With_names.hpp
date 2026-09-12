#pragma once

#include "../meta.hpp"
#include "../string.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>


namespace core::utils::enums {

template< class Enum> requires std::is_enum_v< Enum>
class With_names {
  static_assert(meta::ALWAYS_FALSE< Enum>, "There are no such specialization");
};

template< auto enum_value, string::CVS_constexpr_string_like Name_t> requires std::is_enum_v< decltype(enum_value)>
struct Named_value {
  using Enum = decltype(enum_value);
  using Name = Name_t;
  static constexpr Enum value = enum_value;
};

template< class T>
concept Named_value_like =
  requires {
    typename T::Enum;
    requires string::CVS_constexpr_string_like< typename T::Name>;
    requires std::is_same_v< std::remove_const_t< decltype(T::value)>, typename T::Enum>;
  };

template< Named_value_like... Named_values>
  requires std::is_enum_v< typename meta::Pack_element< 0, Named_values...>::Enum>
class With_names_base {
  using Enum = typename meta::Pack_element< 0, Named_values...>::Enum;
  static_assert(
    (std::is_same_v<
       typename std::tuple_element_t< 0, std::tuple< Named_values...>>::Enum,
       typename Named_values::Enum
     > && ...),
    "All values must be from same Enum"
  );

  // Sort by names
  template< Named_value_like First, Named_value_like Second>
  struct Less_by_name {
    static constexpr bool value = meta::Less< typename First::Name, typename Second::Name>::value;
  };

  using Sort_by_name = typename meta::Sort< std::tuple< Named_values...>, Less_by_name>;
  static_assert(!Sort_by_name::_is_contains_duplicates, "Found duplicates in names");

  // Sort by values
  template< Named_value_like First, Named_value_like Second>
  struct Less_by_value {
    static constexpr bool value = First::value < Second::value;
  };

  using Sort_by_value = typename meta::Sort< std::tuple< Named_values...>, Less_by_value>;
  static_assert(!Sort_by_value::_is_contains_duplicates, "Found duplicates in values");


  template< bool is_value_by_name>
  using Result_type = std::conditional_t< is_value_by_name, Enum, std::string_view>;

  template< bool is_value_by_name>
  using Map = std::unordered_map< Result_type< !is_value_by_name>, Result_type< is_value_by_name>>;

  /*
  template< string::CVS_constexpr_string_like Name>
  static constexpr std::optional< Enum> get_value_by_name() {
    const auto find_result = Sorted_by_name::template find< Name>();
    if constexpr (!find_result) {
      return std::nullopt;
    }

    return std::tuple_element_t< *find_result, typename Sorted_by_name::type>::value;
  }
   */

  template< bool is_value_by_name>
  struct Equal {
    template< Named_value_like First, Named_value_like Second>
    struct type {
      static constexpr bool value =
        is_value_by_name ?
          First::Name::view() == Second::Name::view()
          : First::value == Second::value;
    };
  };

  template< Named_value_like T, bool is_value_by_name>
  static consteval std::optional< Result_type< is_value_by_name>> get_inner() {
    using Sorted_tuple =
      std::conditional_t< is_value_by_name, Sort_by_name, Sort_by_value>;

    constexpr auto find_result = Sorted_tuple::template find< T, Equal< is_value_by_name>::template type>();
    if constexpr (!find_result) {
      return std::nullopt;
    }
    else {
      using Founded_type = std::tuple_element_t< *find_result, typename Sorted_tuple::type>;
      if constexpr (is_value_by_name) {
        return Founded_type::value;
      }
      else {
        return Founded_type::Name::view();
      }
    }
  }

  /*
  static std::optional< Enum> get_value_by_name(const std::string_view name) {
    static std::optional< Map_by_name> map_by_name;
    if (!map_by_name) {
      map_by_name.template emplace({ Named_values::Name::view(), Named_values::value } ... );
    }

    const auto find_result = map_by_name->find(name);
    if (find_result == map_by_name->end()) {
      return std::nullopt;
    }

    return *find_result;
  }
   */

  template< bool is_value_by_name>
  static std::optional< Result_type< is_value_by_name>> get_inner(const Result_type< !is_value_by_name> argument) {
    // Thread-safe one-shot initialization via the function-local static guarantee;
    // a lazy 'if (!map) emplace' would race on concurrent first calls.
    static const Map< is_value_by_name> map = [] {
      if constexpr (is_value_by_name) {
        return Map< is_value_by_name>{ { Named_values::Name::view(), Named_values::value } ... };
      }
      else {
        return Map< is_value_by_name>{ { Named_values::value, Named_values::Name::view() } ... };
      }
    }();

    const auto find_result = map.find(argument);
    if (find_result == map.end()) {
      return std::nullopt;
    }

    return find_result->second;
  }

 public:
  static constexpr auto NAMES =
    string::get_array_of_strings< std::tuple< typename Named_values::Name...>>();
  static constexpr auto SIZE = NAMES.size();
  static constexpr std::array< Enum, SIZE> VALUES = { Named_values::value... };

 public:
  template< string::CVS_constexpr_string_like Name>
  static consteval Enum get_value_by_name() {
    constexpr auto result = get_inner< Named_value< Enum{}, Name>, true>();
    static_assert(result, "Can't find value by name");
    return *result;
  }

  template< Enum value>
  static consteval std::string_view get_name_by_value() {
    constexpr auto result = get_inner< Named_value< value, string::Constexpr_string< "">>, false>();
    static_assert(result, "Can't find name by value");
    return *result;
  }

  static auto get_value_by_name(const std::string_view value) {
    return get_inner< true>(value);
  }

  static auto get_name_by_value(const Enum value) {
    return get_inner< false>(value);
  }
};

// An enum described by a With_names specialization: the single source of the declared
// enumerators and of their count for every container keyed by an enum.
template< class Enum>
concept With_names_like =
  std::is_enum_v< Enum> &&
  requires {
    { With_names< Enum>::SIZE } -> std::convertible_to< std::size_t>;
    { With_names< Enum>::VALUES[0] } -> std::convertible_to< Enum>;
  };

} // namespace core::utils::enums
