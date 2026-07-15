#pragma once

#include "general.hpp"
#include "meta.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>


namespace core::utils::string {

// Compile-time string usable as a non-type template parameter (structural type).
template< std::size_t N >
struct fixed_string {
  char _data[N] {};

  consteval fixed_string(const char (&literal)[N]) {
    std::copy_n(literal, N, _data);
  }

  constexpr std::string_view view() const {
    return { _data, N - 1 };  // strip trailing '\0'
  }

  constexpr auto operator<=>(const fixed_string&) const = default;
};

// Wrapper providing a static 'view()', so a string can be carried as a type argument.
template< fixed_string String >
struct Constexpr_string {
  static constexpr std::string_view view() {
    return String.view();
  }
};

} // namespace core::utils::string

namespace core::utils {

template< string::fixed_string first_string, string::fixed_string second_string >
struct meta::Less<
  string::Constexpr_string< first_string >,
  string::Constexpr_string< second_string >
> {
  static constexpr bool value = first_string.view() < second_string.view();
};

}

namespace core::utils::string {

std::string remove_password_from_address(std::string address);
bool transform_from_local_form(std::string& source, const std::string& target);

template <class T>
concept CVS_constexpr_string_like =
  requires {
    { T::view() } -> std::convertible_to<std::string_view>;
  };

template <class T>
struct Is_CVS_constexpr_string_like {
  static constexpr bool value = CVS_constexpr_string_like< T >;
};

template <class T>
static constexpr bool Is_CVS_constexpr_string_like_v = Is_CVS_constexpr_string_like< T >::value;

template< class T >
concept Tuple_of_CVS_constexpr_string_like = meta::Tuple_of_like< T, Is_CVS_constexpr_string_like  >;


namespace details {

template< Tuple_of_CVS_constexpr_string_like Strings_list, std::size_t... indexes >
static constexpr std::array< std::string_view, std::tuple_size_v< Strings_list > >
get_array_of_strings_inner(std::index_sequence< indexes... >) {
  return {std::tuple_element_t< indexes, Strings_list >::view() ...};
}

}

template<
  Tuple_of_CVS_constexpr_string_like Strings_list,
  typename Indexes = std::make_index_sequence< std::tuple_size_v< Strings_list > >
>
static constexpr std::array< std::string_view, std::tuple_size_v< Strings_list > > get_array_of_strings() {
  return details::get_array_of_strings_inner< Strings_list >(Indexes{});
}

template< class Enum > requires std::is_enum_v<Enum>
class Enum_with_names {
  static_assert(meta::ALWAYS_FALSE< Enum >, "There are no such specialization");
};

template< auto enum_value, CVS_constexpr_string_like Name_t > requires std::is_enum_v< decltype(enum_value) >
struct Named_enum_value {
  using Enum = decltype(enum_value);
  using Name = Name_t;
  static constexpr Enum value = enum_value;
};

template <class T>
concept Named_enum_value_like =
  requires {
    typename T::Enum;
    requires CVS_constexpr_string_like< typename T::Name >;
    requires std::is_same_v< std::remove_const_t< decltype(T::value) >, typename T::Enum >;
  };

template< Named_enum_value_like... Named_values >
  requires std::is_enum_v< typename meta::Pack_element< 0, Named_values... >::Enum >
class Enum_with_names_base {
  using Enum = typename meta::Pack_element< 0, Named_values... >::Enum;
  static_assert(
    (std::is_same_v<
       typename std::tuple_element_t< 0, std::tuple< Named_values... > >::Enum,
       typename Named_values::Enum
     > && ... ),
    "All values must be from same Enum"
  );

  // Sort by names
  template< Named_enum_value_like First, Named_enum_value_like Second >
  struct Less_by_name {
    static constexpr bool value = meta::Less< typename First::Name, typename Second::Name >::value;
  };

  using Sort_by_name = typename meta::Sort< std::tuple< Named_values... >, Less_by_name >;
  static_assert(!Sort_by_name::_is_contains_duplicates, "Found duplicates in names");

  // Sort by values
  template< Named_enum_value_like First, Named_enum_value_like Second >
  struct Less_by_value {
    static constexpr bool value = First::value < Second::value;
  };

  using Sort_by_value = typename meta::Sort< std::tuple< Named_values... >, Less_by_value >;
  static_assert(!Sort_by_value::_is_contains_duplicates, "Found duplicates in values");


  template< bool is_value_by_name >
  using Result_type = std::conditional_t< is_value_by_name, Enum, std::string_view >;

  template< bool is_value_by_name >
  using Map = std::unordered_map< Result_type< !is_value_by_name >, Result_type< is_value_by_name > >;

  /*
  template< CVS_constexpr_string_like Name >
  static constexpr std::optional< Enum > get_value_by_name() {
    const auto find_result = Sorted_by_name::template find< Name >();
    if constexpr (!find_result) {
      return std::nullopt;
    }

    return std::tuple_element_t< *find_result, typename Sorted_by_name::type >::value;
  }
   */

  template< bool is_value_by_name >
  struct Equal {
    template< Named_enum_value_like First, Named_enum_value_like Second >
    struct type {
      static constexpr bool value =
        is_value_by_name ?
          First::Name::view() == Second::Name::view()
          : First::value == Second::value;
    };
  };

  template< Named_enum_value_like T, bool is_value_by_name >
  static consteval std::optional< Result_type< is_value_by_name > > get_inner() {
    using Sorted_tuple =
      std::conditional_t< is_value_by_name, Sort_by_name, Sort_by_value >;

    constexpr auto find_result = Sorted_tuple::template find< T, Equal< is_value_by_name >::template type >();
    if constexpr (!find_result) {
      return std::nullopt;
    }
    else {
      using Founded_type = std::tuple_element_t< *find_result, typename Sorted_tuple::type >;
      if constexpr (is_value_by_name) {
        return Founded_type::value;
      }
      else {
        return Founded_type::Name::view();
      }
    }
  }

  /*
  static std::optional< Enum > get_value_by_name(const std::string_view name) {
    static std::optional< Map_by_name > map_by_name;
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

  template< bool is_value_by_name >
  static std::optional< Result_type< is_value_by_name > > get_inner(const Result_type< !is_value_by_name > argument) {
    // Thread-safe one-shot initialization via the function-local static guarantee;
    // a lazy 'if (!map) emplace' would race on concurrent first calls.
    static const Map< is_value_by_name > map = [] {
      if constexpr (is_value_by_name) {
        return Map< is_value_by_name >{ { Named_values::Name::view(), Named_values::value } ... };
      }
      else {
        return Map< is_value_by_name >{ { Named_values::value, Named_values::Name::view() } ... };
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
    core::utils::string::get_array_of_strings< std::tuple< typename Named_values::Name... > >();
  static constexpr auto SIZE = NAMES.size();

 public:
  template< CVS_constexpr_string_like Name >
  static consteval Enum get_value_by_name() {
    constexpr auto result = get_inner< Named_enum_value< Enum{}, Name >, true >();
    static_assert(result, "Can't find value by name");
    return *result;
  }

  template< Enum value >
  static consteval std::string_view get_name_by_value() {
    constexpr auto result = get_inner< Named_enum_value< value, Constexpr_string<""> >, false >();
    static_assert(result, "Can't find name by value");
    return *result;
  }

  static auto get_value_by_name(const std::string_view value) {
    return get_inner< true >(value);
  }

  static auto get_name_by_value(const Enum value) {
    return get_inner< false >(value);
  }
};

} // namespace core::utils::string


