#pragma once

#include "enum.hpp"
#include "general.hpp"
#include "meta.hpp"
#include "string.hpp"

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>


namespace core::utils {

namespace details {

// A struct rather than a constrained alias: it can carry the static_assert, so a key that is not a declared
// enumerator is reported as such instead of as std::tuple_element's own out-of-range failure. The valid
// case is a constrained specialization, not a static_assert next to the alias: an index outside the tuple
// would otherwise still be formed after the assertion had fired, adding that very failure back on top.
template< class Tuple, Enum_with_names_like Enum, Enum key>
struct Enum_tuple_element {
  static_assert(
    meta::ALWAYS_FALSE< Tuple>,
    "Enum value is not declared in the Enum_with_names specialization"
  );
};

template< class Tuple, Enum_with_names_like Enum, Enum key> requires (is_declared_enumerator(key))
struct Enum_tuple_element< Tuple, Enum, key> {
  using type = std::tuple_element_t< static_cast< std::size_t>(to_underlying(key)), Tuple>;
};

} // namespace details

// One type per enumerator, positionally: type i belongs to the enumerator whose underlying value is i —
// the rule by which Enum_array fills its elements, applied to the types themselves.
template< Enum_with_names_like Enum, class... T>
class Enum_tuple {
  using Names = string::Enum_with_names< Enum>;
  static_assert(
    details::is_dense_enum< Enum>(),
    "Enum_tuple requires a dense enum: values must form a permutation of 0..SIZE-1"
  );

  // Together with density this is the completeness check: the declared values are exactly 0..SIZE-1, so
  // matching their count means every declared enumerator receives a type and no position is left spare.
  static_assert(sizeof...(T) == Names::SIZE, "Enum_tuple requires exactly one type per declared enum value");

  using Tuple = std::tuple< T...>;

  Tuple _data {};

  // Shared by the const and the non-const visit: Tuple_reference carries the constness of the caller.
  // The jump table costs one indirect call regardless of SIZE, where a chain of comparisons would grow
  // with it.
  template< class Tuple_reference, class Functor, std::size_t... indexes>
  static constexpr decltype(auto) dispatch(
    Tuple_reference& data,
    const Enum key,
    Functor&& functor,
    std::index_sequence< indexes...>
  ) {
    // An assertion rather than a constraint on visit(): it names the actual mismatch, where a constraint
    // would only report that the call does not match. The cost is that a badly shaped functor cannot be
    // detected from outside - 'requires { instance.visit(key, functor); }' hard-errors instead of being
    // false - which no caller here needs.
    using Result = std::invoke_result_t< Functor, decltype(std::get< 0>(data))>;
    static_assert(
      (std::same_as< Result, std::invoke_result_t< Functor, decltype(std::get< indexes>(data))>> && ...),
      "Enum_tuple::visit requires the functor to return the same type for every element"
    );

    if (!is_declared_enumerator(key)) {
      throw std::out_of_range("Enum_tuple: enum value is not a declared enumerator");
    }

    using Handler = Result (*)(Tuple_reference&, Functor&&);
    static constexpr std::array< Handler, sizeof...(indexes)> handlers = {
      +[](Tuple_reference& data_inner, Functor&& functor_inner) -> Result {
        return std::invoke(std::forward< Functor>(functor_inner), std::get< indexes>(data_inner));
      } ...
    };

    return handlers[static_cast< std::size_t>(to_underlying(key))](data, std::forward< Functor>(functor));
  }

  // The functor is called once per element, so it is taken by reference and never forwarded: forwarding it
  // would hand the same object to every element after it had possibly been moved from.
  template< class Tuple_reference, class Functor, std::size_t... indexes>
  static constexpr void for_each_inner(Tuple_reference& data, Functor& functor, std::index_sequence< indexes...>) {
    static_assert(
      (std::invocable<
         Functor&,
         decltype(std::get< indexes>(data)),
         std::integral_constant< Enum, static_cast< Enum>(indexes)>
       > && ...),
      "Enum_tuple::for_each requires the functor to accept every element together with its key"
    );

    (std::invoke(functor, std::get< indexes>(data), std::integral_constant< Enum, static_cast< Enum>(indexes)>{}), ...);
  }

 public:
  static constexpr std::size_t SIZE = Names::SIZE;

  // Part of the interface: at< key>() returns it, and a caller needs to be able to name it.
  template< Enum key>
  using Element = typename details::Enum_tuple_element< Tuple, Enum, key>::type;

  constexpr Enum_tuple() = default;

  // Arguments come in enum order — argument i initializes the element of the enumerator with underlying
  // value i — whatever order the enumerators were declared in inside the Enum_with_names specialization.
  //
  // The 'sizeof...(Args) > 1' guard keeps this template from hijacking the copy/move constructor when
  // SIZE == 1 and the element type has a greedy converting constructor (std::any, std::variant, ...).
  // Only the single-element form is explicit: there it guards against an unintended element-to-Enum_tuple
  // conversion. Unlike Enum_array there is no initializer_list constructor to compete with a braced list —
  // elements are heterogeneous — so '{...}' and '(...)' reach this same constructor, and a wrong argument
  // count is always a compile-time error.
  //
  // Each argument builds its element in place, with no intermediate object to copy or move: std::tuple
  // direct-initializes every element from its argument, which also accepts an explicit constructor.
  //
  // The two halves of the per-element requirement differ on purpose. 'T(argument)' is the form the element
  // is actually built with, so 'std::vector< double>(3)' stays a vector of three rather than the
  // one-element list the braced form would produce. 'T{argument}' is required in addition purely for its
  // narrowing rules, which the parenthesized form does not have: without it a double argument would
  // silently truncate into an int element.
  template< class... Args>
    requires
      (sizeof...(Args) == SIZE)
      && (sizeof...(Args) > 1 || !(std::same_as< Enum_tuple, std::remove_cvref_t< Args>> && ...))
      && (requires (Args&& argument) {
            T(std::forward< Args>(argument));
            T{std::forward< Args>(argument)};
          } && ...)
  explicit(SIZE == 1) constexpr Enum_tuple(Args&&... args) : _data(std::forward< Args>(args)...) {}

  // Key known at compile time: validity is proven, so the access needs no check and cannot throw. There is
  // no run-time counterpart — the type of the result depends on the key, so a run-time key can only be
  // served by visit() below.
  template< Enum key>
  constexpr Element< key>& at() noexcept {
    return std::get< static_cast< std::size_t>(to_underlying(key))>(_data);
  }

  template< Enum key>
  constexpr const Element< key>& at() const noexcept {
    return std::get< static_cast< std::size_t>(to_underlying(key))>(_data);
  }

  // The enumerator's name from the Enum_with_names specialization in place of the enumerator itself, for
  // when the key arrives as a string template argument. The name is resolved at compile time, so nothing
  // of it survives to run time; a name that was never declared fails inside get_value_by_name.
  template< string::CVS_constexpr_string_like Name>
  constexpr auto& at() noexcept {
    return at< Names::template get_value_by_name< Name>()>();
  }

  template< string::CVS_constexpr_string_like Name>
  constexpr const auto& at() const noexcept {
    return at< Names::template get_value_by_name< Name>()>();
  }

  // The only way to an element by a key known at run time, and the only place where such a key can be
  // wrong: it may be cast from an arbitrary integer, or be an enumerator that Enum_with_names never
  // declared, so an invalid key throws exactly as Enum_array::at(Enum) does. The functor must return the
  // same type for every element — the type of an expression cannot depend on a run-time value.
  template< class Functor>
  constexpr decltype(auto) visit(const Enum key, Functor&& functor) {
    return dispatch(_data, key, std::forward< Functor>(functor), std::make_index_sequence< SIZE>{});
  }

  template< class Functor>
  constexpr decltype(auto) visit(const Enum key, Functor&& functor) const {
    return dispatch(_data, key, std::forward< Functor>(functor), std::make_index_sequence< SIZE>{});
  }

  // Every element in enum order. The key travels with the element as a compile-time constant, so the
  // functor can tell the elements apart: look the name up through Enum_with_names, reach the same key in
  // another Enum_tuple, and so on.
  template< class Functor>
  constexpr void for_each(Functor&& functor) {
    for_each_inner(_data, functor, std::make_index_sequence< SIZE>{});
  }

  template< class Functor>
  constexpr void for_each(Functor&& functor) const {
    for_each_inner(_data, functor, std::make_index_sequence< SIZE>{});
  }

  // Forward to the underlying tuple. Written as ordinary members (not '= default') so their bodies
  // instantiate lazily, only where a comparison is actually used: for an element type that cannot be
  // compared the operators simply never come into existence, whereas a defaulted comparison is
  // instantiated together with the class and would hard-error inside std::tuple's own comparison.
  constexpr bool operator == (const Enum_tuple& other) const {
    return _data == other._data;
  }

  constexpr auto operator <=> (const Enum_tuple& other) const {
    return _data <=> other._data;
  }

  // Tuple protocol, in enum order: index i is the element of the enumerator with underlying value i. It is
  // what structured bindings and anything else reaching elements by position need; the keys do not survive
  // it, so at< key>() stays the way to say which element is meant.
  //
  // Hidden friends: found by argument-dependent lookup only, which is exactly how both structured bindings
  // and an unqualified 'get< index>(tuple)' look the name up, and they reach _data without opening it to
  // anyone else. std::apply stays out of reach whatever we do here - it is specified to call std::get,
  // which cannot be extended for a program-defined type; for_each() covers what it would have been used for.
  template< std::size_t index> requires (index < sizeof...(T))
  friend constexpr std::tuple_element_t< index, Tuple>& get(Enum_tuple& tuple) noexcept {
    return std::get< index>(tuple._data);
  }

  template< std::size_t index> requires (index < sizeof...(T))
  friend constexpr const std::tuple_element_t< index, Tuple>& get(const Enum_tuple& tuple) noexcept {
    return std::get< index>(tuple._data);
  }

  template< std::size_t index> requires (index < sizeof...(T))
  friend constexpr std::tuple_element_t< index, Tuple>&& get(Enum_tuple&& tuple) noexcept {
    return std::get< index>(std::move(tuple._data));
  }

  template< std::size_t index> requires (index < sizeof...(T))
  friend constexpr const std::tuple_element_t< index, Tuple>&& get(const Enum_tuple&& tuple) noexcept {
    return std::get< index>(std::move(tuple._data));
  }
};

} // namespace core::utils

namespace std {

template< core::utils::Enum_with_names_like Enum, class... T>
struct tuple_size< core::utils::Enum_tuple< Enum, T...>> : integral_constant< size_t, sizeof...(T)> {};

// Split into a constrained specialization and a plain one for the same reason details::Enum_tuple_element
// is: forming the index anyway would answer an out-of-range one with std::tuple's own assertion rather
// than with a message naming this container.
template< size_t index, core::utils::Enum_with_names_like Enum, class... T>
struct tuple_element< index, core::utils::Enum_tuple< Enum, T...>> {
  static_assert(
    core::utils::meta::ALWAYS_FALSE< tuple< T...>>,
    "Enum_tuple: element index is out of range"
  );
};

template< size_t index, core::utils::Enum_with_names_like Enum, class... T> requires (index < sizeof...(T))
struct tuple_element< index, core::utils::Enum_tuple< Enum, T...>> {
  using type = tuple_element_t< index, tuple< T...>>;
};

} // namespace std
