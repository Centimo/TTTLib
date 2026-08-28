#pragma once

#include "enum.hpp"
#include "general.hpp"
#include "string.hpp"

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>


namespace core::utils {

// Names the alternative to build, the way std::in_place_index does for std::variant: the key picks the
// slot, the remaining arguments build the element in it.
template< auto key> requires std::is_enum_v< decltype(key)>
struct In_place_key {};

template< auto key> requires std::is_enum_v< decltype(key)>
inline constexpr In_place_key< key> in_place_key {};

namespace details {

template< class T>
struct Is_in_place_key : std::false_type {};

template< auto key>
struct Is_in_place_key< In_place_key< key>> : std::true_type {};

// What it takes to build an element from the arguments that came with its key. The parenthesized form is
// how the element is actually built. The braced form is asked for in addition only when there is a single
// argument: there the argument is the value, and a narrowing conversion would silently replace it, which is
// what the keyless constructor rejects through std::variant's own rules. Several arguments name a
// constructor instead, and demanding the braced form of them would test a different construction
// altogether - 'std::vector< double>{3, 1.5}' is a two-element list, not three copies of 1.5.
template< class Element, class... Args>
concept Buildable_element =
  requires (Args&&... args) { Element(std::forward< Args>(args)...); }
  && (sizeof...(Args) != 1 || requires (Args&&... args) { Element{std::forward< Args>(args)...}; });

} // namespace details

// One type per enumerator, positionally, of which exactly one is alive at a time: the active enumerator is
// the discriminant. Where Enum_tuple holds every element at once, this one holds a single element and
// remembers whose it is.
template< Enum_with_names_like Enum, class... T>
class Enum_variant {
  using Names = string::Enum_with_names< Enum>;
  static_assert(
    details::is_dense_enum< Enum>(),
    "Enum_variant requires a dense enum: values must form a permutation of 0..SIZE-1"
  );

  // Together with density this is the completeness check: the declared values are exactly 0..SIZE-1, so
  // matching their count means every declared enumerator receives a type and no position is left spare.
  static_assert(sizeof...(T) == Names::SIZE, "Enum_variant requires exactly one type per declared enum value");

  // What keeps a keyless state impossible. std::variant loses its value only when it has to rebuild an
  // element in place and that construction throws; given a move that cannot throw, both its own
  // assignments and emplace() below go through a temporary instead, so a throwing construction leaves the
  // previous element where it was. active_key() therefore always has an answer.
  static_assert(
    (std::is_nothrow_move_constructible_v< T> && ...),
    "Enum_variant requires alternatives whose move constructor cannot throw"
  );

  using Variant = std::variant< T...>;
  using Types = std::tuple< T...>;

  Variant _data;

  static constexpr std::size_t index_of(const Enum key) noexcept {
    return static_cast< std::size_t>(to_underlying(key));
  }

  // The key an alternative belongs to, as the compile-time constant the functor of visit() receives.
  template< std::size_t index>
  using Key_constant = std::integral_constant< Enum, static_cast< Enum>(index)>;

  // What the functor gives back for one alternative: the element with the caller's constness, plus its key.
  template< class Variant_reference, class Functor, std::size_t index>
  using Visit_result = std::invoke_result_t<
    Functor,
    decltype(std::get< index>(std::declval< Variant_reference&>())),
    Key_constant< index>
  >;

  // Shared by the const and the non-const visit: Variant_reference carries the constness of the caller.
  // Indexing is by the alternative that is actually alive, so std::get on it cannot throw. The jump table
  // costs one indirect call regardless of SIZE, where a chain of comparisons would grow with it.
  template< class Variant_reference, class Functor, std::size_t... indexes>
  static constexpr decltype(auto) dispatch(
    Variant_reference& data,
    Functor&& functor,
    std::index_sequence< indexes...>
  ) {
    // An assertion rather than a constraint on visit(): it names the actual mismatch, where a constraint
    // would only report that the call does not match.
    using Result = Visit_result< Variant_reference, Functor, 0>;
    static_assert(
      (std::same_as< Result, Visit_result< Variant_reference, Functor, indexes>> && ...),
      "Enum_variant::visit requires the functor to return the same type for every alternative"
    );

    using Handler = Result (*)(Variant_reference&, Functor&&);
    static constexpr std::array< Handler, sizeof...(indexes)> handlers = {
      +[](Variant_reference& data_inner, Functor&& functor_inner) -> Result {
        return std::invoke(
          std::forward< Functor>(functor_inner),
          std::get< indexes>(data_inner),
          Key_constant< indexes>{}
        );
      } ...
    };

    return handlers[data.index()](data, std::forward< Functor>(functor));
  }

 public:
  static constexpr std::size_t SIZE = Names::SIZE;

  // Part of the interface: at< key>() returns it, and a caller needs to be able to name it.
  template< Enum key>
  using Element = typename details::Enum_element< Types, Enum, key>::type;

  // The enumerator with underlying value 0 starts out active, since std::variant default-constructs its
  // first alternative.
  constexpr Enum_variant() = default;

  // The alternative follows from the argument, by std::variant's own rules: the imaginary overload set
  // over the alternatives has to pick exactly one. A type appearing once in the list is thus reachable
  // without naming its key; a type appearing twice makes that set ambiguous and can only be built through
  // In_place_key. The same rules reject a narrowing argument, which Enum_tuple's constructor has to ask
  // for separately.
  //
  // Implicit, where Enum_tuple's single-argument constructor is explicit. There the argument is one element
  // of several and the conversion would be a guess; here the element is the whole value, and 'Message
  // message = "text"' is the shape std::variant taught everyone to expect. The copy constructor is shielded
  // by the first clause below rather than by explicitness.
  template< class Argument>
    requires
      (!std::same_as< Enum_variant, std::remove_cvref_t< Argument>>)
      && (!details::Is_in_place_key< std::remove_cvref_t< Argument>>::value)
      && std::constructible_from< Variant, Argument>
  constexpr Enum_variant(Argument&& argument) : _data(std::forward< Argument>(argument)) {}

  // Naming the key must not buy a silent truncation that the keyless constructor above rejects, hence
  // Buildable_element rather than plain constructibility.
  template< Enum key, class... Args> requires details::Buildable_element< Element< key>, Args...>
  constexpr Enum_variant(In_place_key< key>, Args&&... args)
    : _data(std::in_place_index< index_of(key)>, std::forward< Args>(args)...)
  {}

  // Replaces whatever is alive with the element of 'key'. std::variant::emplace destroys the old element
  // before building the new one, which costs two things at once: a construction that throws would leave no
  // value at all, and an argument referring to the old element would be read after it had been destroyed
  // ('variant.emplace< index>(variant.at< key>())'). Building the element first and moving it in answers
  // both - the argument is consumed while the old element is still alive, and by the time that one is
  // destroyed only a move remains, which cannot throw. The move is why this is not done in place even when
  // the construction itself is harmless: the distinction would reopen the aliasing hole.
  template< Enum key, class... Args> requires details::Buildable_element< Element< key>, Args...>
  constexpr Element< key>& emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v< Element< key>, Args...>) {
    Element< key> value(std::forward< Args>(args)...);
    return _data.template emplace< index_of(key)>(std::move(value));
  }

  // Always a declared enumerator: the alternatives rule out the keyless state std::variant would otherwise
  // report through valueless_by_exception().
  constexpr Enum active_key() const noexcept {
    return static_cast< Enum>(_data.index());
  }

  template< Enum key>
  constexpr bool holds() const noexcept {
    static_assert(is_declared_enumerator(key), "Enum value is not declared in the Enum_with_names specialization");
    return _data.index() == index_of(key);
  }

  // Throws std::bad_variant_access when another key is active — the element of a key that is not alive
  // does not exist, so there is nothing to return. at_if() below is the answer that does not throw.
  template< Enum key>
  constexpr Element< key>& at() {
    return std::get< index_of(key)>(_data);
  }

  template< Enum key>
  constexpr const Element< key>& at() const {
    return std::get< index_of(key)>(_data);
  }

  // The enumerator's name from the Enum_with_names specialization in place of the enumerator itself, for
  // when the key arrives as a string template argument.
  template< string::CVS_constexpr_string_like Name>
  constexpr auto& at() {
    return at< Names::template get_value_by_name< Name>()>();
  }

  template< string::CVS_constexpr_string_like Name>
  constexpr const auto& at() const {
    return at< Names::template get_value_by_name< Name>()>();
  }

  template< Enum key>
  Optional_reference< Element< key>> at_if() noexcept {
    if (!holds< key>()) {
      return {};
    }

    return std::get< index_of(key)>(_data);
  }

  template< Enum key>
  Optional_reference< const Element< key>> at_if() const noexcept {
    if (!holds< key>()) {
      return {};
    }

    return std::get< index_of(key)>(_data);
  }

  // The element that is alive, together with its key as a compile-time constant — which is the whole point
  // here, since the caller has no other way to know which alternative it was handed. Every alternative must
  // yield the same result type: the type of an expression cannot depend on which one is active.
  template< class Functor>
  constexpr decltype(auto) visit(Functor&& functor) {
    return dispatch(_data, std::forward< Functor>(functor), std::make_index_sequence< SIZE>{});
  }

  template< class Functor>
  constexpr decltype(auto) visit(Functor&& functor) const {
    return dispatch(_data, std::forward< Functor>(functor), std::make_index_sequence< SIZE>{});
  }

  // Forward to the underlying variant, which orders by the active alternative first and compares the
  // elements only for a matching one. Written as ordinary members (not '= default') so their bodies
  // instantiate lazily, only where a comparison is actually used: for an element type that cannot be
  // compared the operators simply never come into existence.
  constexpr bool operator == (const Enum_variant& other) const {
    return _data == other._data;
  }

  constexpr auto operator <=> (const Enum_variant& other) const {
    return _data <=> other._data;
  }
};

} // namespace core::utils
