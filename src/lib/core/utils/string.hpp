#pragma once

#include "general.hpp"
#include "meta.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <tuple>


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

} // namespace core::utils::string


