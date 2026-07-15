#pragma once

#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>


namespace core::utils::meta {

template< class... T>
constexpr bool ALWAYS_FALSE = false;

template< class T >
struct Phantom_type {
  using type = T;
};

// doesn't work yet...
/*
#define CVS_POOL_SAFETY_LAMBDA_FROM_CONCEPT(Concept)             \
  [] <typename LAMBDA_FROM_CONCEPT_INNER> () consteval -> bool { \
    return Concept< LAMBDA_FROM_CONCEPT_INNER >;                 \
  }

template< auto lambda > // requires std::predicate<decltype(lambda)>
struct To_template {
  template< class T >
  struct type {
    static constexpr bool value = lambda.template operator()< T >();
  };
};
 */

template< class T, class Maker = T >
concept Makeable =
  requires (Maker maker) { { maker.make() } -> std::convertible_to<T>;  }
  || requires { { Maker::make() } -> std::convertible_to<T>; };

template< class T, std::size_t size >
concept Has_tuple_element =
  requires(T t) {
    typename std::tuple_element_t< size, std::remove_const_t< T> >;
    { get< size >(t) } -> std::convertible_to< const std::tuple_element_t< size, T>& >;
  };

template <class T>
concept Tuple_like =
  !std::is_reference_v< T >
  && requires(T t) {
    typename std::tuple_size< T >::type;
    requires std::derived_from<
      std::tuple_size< T >,
      std::integral_constant< std::size_t, std::tuple_size_v< T > >
    >;
  }
  && [] <std::size_t... indexes>(std::index_sequence< indexes...>) {
    return (Has_tuple_element< T, indexes> && ...);
  } (std::make_index_sequence<std::tuple_size_v<T>>());

template<
  Tuple_like Tuple,
  template< Tuple_like, size_t > class Iteration_function,
  template< class... > class Cycle_function,
  size_t... indexes
>
constexpr auto tuple_to_variadic(std::index_sequence< indexes...>) {
  using Result = Cycle_function< Iteration_function< Tuple, indexes > ... >;
  return Phantom_type< Result >{};
}

template< size_t index = 0, class... T > requires (index < std::tuple_size_v< std::tuple< T... > >)
using Pack_element = std::tuple_element_t< index, std::tuple< T... > >;

template< class T, template< class > class Concept >
concept Tuple_of_like =
  Tuple_like< T >
  && [] <std::size_t... indexes>(std::index_sequence< indexes... >) consteval -> bool {
    return (Concept< std::tuple_element_t< indexes, T > >::value && ...);
  } (std::make_index_sequence< std::tuple_size_v< T > >());

template< template < class... > class T, class... Test_class >
concept Predicate_like = std::is_nothrow_convertible_v< decltype(T< Test_class... >::value), bool>;

template< class T, template < class > class Predicate >
concept Is_acceptable_as_argument_of_predicate = std::is_nothrow_convertible_v< decltype(Predicate< T >::value), bool>;

namespace details {

template< template < class T> class Predicate, Tuple_like Tuple, size_t... indexes >
  requires
    (Predicate_like< Predicate, std::tuple_element_t< indexes, Tuple > > && ...)
constexpr auto filter_types_list_helper(std::index_sequence<indexes...>) {
  using Result = decltype(std::tuple_cat(
    std::declval<
      std::conditional_t<
        Predicate< std::tuple_element_t< indexes, Tuple > >::value,
        std::tuple< std::tuple_element_t< indexes, Tuple > >,
        std::tuple<>
      >
    >() ...
  ));
  return Phantom_type< Result >{};
};

template< template < class T> class Predicate, Tuple_like Tuple >
class Filter_types_list_base {
  static constexpr size_t tuple_size = std::tuple_size_v< Tuple >;

  template< Tuple_like Tuple_inner, size_t index >
  using Iteration_function = std::conditional_t<
    Predicate< std::tuple_element_t< index, Tuple_inner > >::value,
    std::tuple< std::tuple_element_t< index, Tuple_inner > >,
    std::tuple<>
  >;

  template< class... T_inner >
  using Cycle_function = decltype(std::tuple_cat(std::declval< T_inner >() ...));

 public:
  using type = typename decltype(tuple_to_variadic< Tuple, Iteration_function, Cycle_function >(std::make_index_sequence< tuple_size >{}))::type;
    //typename decltype(
    //  details::filter_types_list_helper< Predicate, Tuple>(std::make_index_sequence< tuple_size >{})
    //)::type;
};

} // namespace details

template< template < class T> class Predicate, Tuple_like Tuple >
class Filter_types_list : public details::Filter_types_list_base< Predicate, Tuple > {};

template< template < class T> class Predicate, class... Arguments >
  requires
    (Predicate_like< Predicate, Arguments > && ... )
class Filter_types_list< Predicate, std::tuple< Arguments... > >
  : public details::Filter_types_list_base< Predicate, std::tuple< Arguments... > > {};

template< class First, class Second >
class Less {
  static_assert(ALWAYS_FALSE< First, Second >, "Less is not implemented for these classes");
};

// template< template < class > class T, class Test_class >
// concept Comparable_less = std::is_same_v< decltype(T< Test_class >::value), bool>;

namespace details {

template< Tuple_like Tuple, size_t start_index, size_t... indexes>
struct Make_subtuple_inner {
  using type = std::tuple< std::tuple_element_t< start_index + indexes, Tuple > ... >;
};

} // namespace details

template< Tuple_like Tuple, size_t start_index, size_t end_index>
struct Make_subtuple {
  static_assert(start_index < end_index, "start_index must be less than end_index");
  static_assert(end_index <= std::tuple_size_v< Tuple >, "end_index must be less than or equal to tuple size");
  // using type = details::Make_subtuple_inner< Tuple, std::integer_sequence< size_t, end_index - start_index >{} >;
  template< Tuple_like Tuple_INNER, size_t index_INNER >
  using Iteration_function = std::tuple_element_t< start_index + index_INNER, Tuple_INNER >;

  template< class... T_inner >
  using Cycle_function = typename std::tuple< T_inner... >;

  using type = typename decltype(
    tuple_to_variadic< Tuple, Iteration_function, Cycle_function >(
      std::make_index_sequence< end_index - start_index >{}
    )
  )::type;
};

/*
template< Tuple_like Tuple, size_t split_index >
class Split {
  static constexpr size_t tuple_size = std::tuple_size_v< Tuple >;
  static_assert(split_index > 0 && split_index < tuple_size, "split_index must be in (0, tuple_size)");
};
 */

namespace details {

template<
  Tuple_like Tuple,
  class T,
  template< class, class > class Less_l,
  template< class, class > class Is_equal
>
class Binary_search {
  static constexpr size_t tuple_size = std::tuple_size_v< Tuple >;
  static constexpr size_t pivot_index = tuple_size / 2;

  using Pivot_element = std::tuple_element_t< pivot_index, Tuple >;

  static constexpr std::optional< size_t > make_result() {
    if constexpr (Is_equal< T, Pivot_element >::value) {
      return pivot_index;
    }

    if constexpr (Less_l< T, Pivot_element >::value) {
      using Left_subtuple = typename Make_subtuple< Tuple, 0, pivot_index >::type;
      return Binary_search< Left_subtuple, T, Less_l, Is_equal >::value;
    }
    else {
      using Right_subtuple = typename Make_subtuple< Tuple, pivot_index + 1, tuple_size >::type;
      constexpr auto right_subtuple_search_result = Binary_search< Right_subtuple, T, Less_l, Is_equal >::value;
      if constexpr (!right_subtuple_search_result) {
        return right_subtuple_search_result;
      }

      return *right_subtuple_search_result + tuple_size - std::tuple_size_v< Right_subtuple >;
    }
  }

 public:
  static constexpr std::optional< size_t > value = make_result();
};

template<
  Tuple_like Tuple,
  class T,
  template< class, class > class Less_l,
  template< class, class > class Is_equal
>
  requires (std::tuple_size_v< Tuple > < 3)
class Binary_search< Tuple, T, Less_l, Is_equal > {
  static constexpr size_t tuple_size = std::tuple_size_v< Tuple >;

  static constexpr std::optional< size_t > make_result() {
    if constexpr (tuple_size == 0) {
      return std::nullopt;
    }

    if constexpr (tuple_size > 0) {
      if constexpr (Is_equal< std::tuple_element_t< 0, Tuple >, T >::value) {
        return 0;
      }
    }

    if constexpr (tuple_size > 1) {
      if constexpr (Is_equal< std::tuple_element_t< 1, Tuple >, T >::value) {
        return 1;
      }
    }

    return std::nullopt;
  }

 public:
  static constexpr std::optional< size_t > value = make_result();
};

template< Tuple_like Tuple, size_t... indexes >
static constexpr bool is_contains_duplicates(std::index_sequence< indexes... >) {
  constexpr size_t tuple_size = std::tuple_size_v< Tuple >;
  static_assert(tuple_size > sizeof...(indexes), "tuple_size must be greater than index_sequence length");

  if constexpr (std::tuple_size_v< Tuple > < 2) {
    return false;
  }

  if constexpr (!(
    std::is_same_v<
      std::tuple_element_t< indexes, Tuple >,
      std::tuple_element_t< indexes + 1, Tuple >
    >
    || ...)
  ) {
    return false;
  }

  return true;
}

} // namespace details


// Quick sort
template< Tuple_like Tuple, template< class, class > class Less_l = Less >
class Sort {
  static constexpr size_t _tuple_size = std::tuple_size_v< Tuple >;
  using Pivot_element = std::tuple_element_t< 0, Tuple >;
  using Tuple_without_pivot = typename Make_subtuple< Tuple, 1, _tuple_size >::type;

  template< class T_INNER >
  using Less_bind = Less_l< T_INNER, Pivot_element >;

  using Less_than_pivot = typename Filter_types_list< Less_bind, Tuple_without_pivot >::type;

  template< class T_INNER >
  struct Greater_or_equal_bind {
    static constexpr bool value = !Less_l < T_INNER, Pivot_element >::value;
  };

  using Greater_than_or_equeal_to_pivot = typename Filter_types_list< Greater_or_equal_bind, Tuple_without_pivot >::type;

 public:
  using type = decltype(std::tuple_cat(
    std::declval< typename Sort< Less_than_pivot, Less_l >::type >(),
    std::declval< std::tuple< Pivot_element > >(),
    std::declval< typename Sort< Greater_than_or_equeal_to_pivot, Less_l >::type >()
  ));

  template< class T, template< class, class > class Is_equal = std::is_same >
  static constexpr std::optional< size_t > find() {
    return details::Binary_search< Tuple, T, Less_l, Is_equal >::value;
  }

  static constexpr bool _is_contains_duplicates =
    details::is_contains_duplicates< Tuple >(std::make_index_sequence< _tuple_size - 1 >{});
};

template< Tuple_like Tuple, template< class, class > class Less_l >
  requires (std::tuple_size_v< Tuple > < 2)
class Sort< Tuple, Less_l > {
 public:
  using type = Tuple;

  template< class T, template< class, class > class Is_equal = std::is_same >
  static constexpr std::optional< size_t > find() {
    if constexpr (std::tuple_size_v< Tuple > == 0) {
      return std::nullopt;
    }

    if constexpr (!Is_equal< std::tuple_element_t< 0, Tuple >, T >::value) {
      return std::nullopt;
    }

    return 0;
  }

  static constexpr bool _is_contains_duplicates = false;
};

} // namespace core::utils