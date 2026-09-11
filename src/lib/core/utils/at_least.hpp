#pragma once

#include "general.hpp"

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <expected>
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>


namespace core::utils::details {

// std::vector's own operator<=> only demands operator< from its element type (synthesized three-way order),
// not std::three_way_comparable; requiring the stronger concept here would reject a T that Container itself
// happily orders.
template< class T>
concept Synthesized_three_way_comparable =
  std::three_way_comparable< T>
  || requires (const T& left, const T& right) {
    { left < right } -> std::convertible_to< bool>;
  };

// default_initializable is required since _data, and every temporary built alongside it (make()'s local
// Container, extract()'s and fill_from_range()'s temporaries), are default-constructed.
template< class Container>
concept Suitable_container =
  std::ranges::bidirectional_range< Container>
  && std::ranges::sized_range< Container>
  && std::default_initializable< Container>;

// Matches an argument that is itself a range of T: used to keep the variadic constructor's single-argument
// overload from swallowing a whole range of elements as if it were one element. Defined as its own concept
// (rather than inlined into a fold-expression) so that a non-range Argument short-circuits before
// range_value_t< Argument> is ever formed for it — range_value_t is ill-formed for a non-range type.
template< class Argument, class Element>
concept Range_of = std::ranges::input_range< Argument> && std::same_as< std::ranges::range_value_t< Argument>, Element>;

} // namespace core::utils::details

namespace core::utils {

template< std::size_t minimal_size, class T, details::Suitable_container Container>
  requires std::same_as< std::ranges::range_reference_t< Container>, T&>
class At_least;

enum class Side { FRONT, BACK };

template< class T>
struct Is_at_least : std::false_type {};

template< std::size_t minimal_size, class T, details::Suitable_container Container>
struct Is_at_least< At_least< minimal_size, T, Container>> : std::true_type {};

template< class T>
concept At_least_like = Is_at_least< std::remove_cvref_t< T>>::value;

template< class Type, std::size_t required_minimal_size, class Element>
concept At_least_of =
  At_least_like< Type>
  && (std::remove_cvref_t< Type>::MINIMAL_SIZE >= required_minimal_size)
  && std::same_as< typename std::remove_cvref_t< Type>::value_type, Element>;

} // namespace core::utils

namespace core::utils::details {

// A shared passkey: the constructor of At_least_key is private, and every instantiation of At_least is its
// friend, so nobody outside At_least can produce one. It lives at namespace scope, shared by every
// instantiation of At_least, rather than nested per instantiation: transform() and zip() build a result of a
// different At_least< ..., U, ...> than 'this', and the converting constructor builds a weaker At_least from
// a stronger one — both need to hand a passkey to an instantiation that is not their own. Only make(),
// make_filled(), release_at_least(), transform() and zip() ever produce one, after the size (or count) check
// has already passed, so reaching the constructor that takes it is itself the proof that the invariant
// holds. That constructor has to stay public rather than private-plus-friending std::optional/std::expected:
// their in-place constructors are gated by std::is_constructible, and that check does not honour a friend
// declaration naming a specific std::optional/std::expected specialization.
class At_least_key {
  template< std::size_t minimal_size, class T, Suitable_container Container>
    requires std::same_as< std::ranges::range_reference_t< Container>, T&>
  friend class core::utils::At_least;

  constexpr At_least_key() = default;
};

} // namespace core::utils::details

namespace core::utils {

/// Wraps a Container so that at least MINIMAL_SIZE elements are present at every moment of the object's
/// life. The invariant is enforced only through the interface: there is no default constructor, and every
/// operation that could drop the element count below MINIMAL_SIZE refuses instead of running, leaving the
/// object unchanged.
///
/// make(), make_filled() and filled() report failure through std::expected< At_least, Container>,
/// std::optional< At_least> and (being proven by a compile-time count) never respectively; on make()'s
/// refusal the same container comes back as the error, so the caller does not lose the data it tried to
/// wrap. release_at_least() keeps std::optional, since its failure carries no useful payload beyond "no".
///
/// release_surplus(), release_at_least() and release_up_to() all take a Side: Side::BACK (the default) keeps
/// the front MINIMAL_SIZE elements and moves out the tail; Side::FRONT keeps the back MINIMAL_SIZE elements
/// and moves out the head — on a std::vector this shifts the remaining elements down, the same way erase()
/// at the beginning does. 'count == 0' means "all surplus" for release_surplus() and release_at_least();
/// release_up_to() takes 'count' literally instead (0 moves nothing) and never refuses, moving
/// std::min(count, surplus) elements. release_at_least() additionally refuses when the moved-out part itself
/// would hold fewer than its own result_minimal_size elements.
///
/// The move constructor and move assignment are deleted: moving _data out would leave the source below
/// MINIMAL_SIZE, which the public interface must never allow — a moved-from container is only guaranteed to
/// be valid for its own type, not "at least MINIMAL_SIZE" for this wrapper. A whole container is taken in
/// only through make() or make_filled(), which check its size before it ever reaches _data; elements move
/// out only through the release_* family, which keep MINIMAL_SIZE elements behind.
///
/// at(index) mirrors std::vector::at and is the only member that throws to report a failure of its own;
/// growing operations may of course still throw from allocation or element construction. try_at(index) is
/// at(index)'s non-throwing counterpart, returning an empty Optional_reference instead. Every other fallible
/// operation reports refusal through std::optional (or std::expected for make()) instead. The iterator-taking
/// members (insert, emplace, erase) carry the same preconditions as the matching Container member, and the
/// iterators they return are invalidated the same way Container's would be. The same holds for references
/// and views returned by front(), back(), at(), try_at(), operator[](), head() and surplus(): they are
/// invalidated exactly when Container's own would be, i.e. by any mutating member.
template< std::size_t minimal_size, class T, details::Suitable_container Container = std::vector< T>>
  requires std::same_as< std::ranges::range_reference_t< Container>, T&>
class At_least {
  static_assert(minimal_size > 0, "At_least< 0, ...> is just the container: use it directly");
  static_assert(std::same_as< typename Container::value_type, T>, "Container::value_type must match T");

 public:
  static constexpr std::size_t MINIMAL_SIZE = minimal_size;
  using value_type = typename Container::value_type;
  using iterator = typename Container::iterator;
  using const_iterator = typename Container::const_iterator;
  using size_type = typename Container::size_type;

 private:
  Container _data;

  // Grows a container's capacity ahead of inserting 'count' more elements, when it supports reserve() at
  // all (std::list does not). Templated on the container type so that _data, a factory's local Container,
  // and transform()/zip()'s Result_container can all share this logic.
  template< class Any_container>
  static constexpr void reserve_additional(Any_container& container, const std::size_t count) {
    if constexpr (requires { container.reserve(container.size() + count); }) {
      container.reserve(container.size() + count);
    }
  }

  // Reserves capacity when the range is sized, then walks the range once, emplacing each element into
  // 'container' in order. Shared by make(std::from_range, ...), append_range() and insert_range(), which all
  // need exactly this. A range-based for loop is used rather than std::ranges::for_each: with libstdc++ 14,
  // std::ranges::for_each passes the element to the function as an lvalue even when the iterator dereferences
  // to an rvalue reference (checked with std::views::as_rvalue); a range-based for loop keeps the category.
  template< std::ranges::input_range Range>
    requires requires (Container& container, std::ranges::range_reference_t< Range> value) {
      container.emplace_back(std::forward< std::ranges::range_reference_t< Range>>(value));
    }
  static constexpr void fill_from_range(Container& container, Range&& range) {
    if constexpr (std::ranges::sized_range< Range>) {
      reserve_additional(container, static_cast< std::size_t>(std::ranges::size(range)));
    }

    for (auto&& value : range) {
      container.emplace_back(std::forward< decltype(value)>(value));
    }
  }

  // Calls erase_if through unqualified lookup, so that ADL finds the overload for whatever Container the
  // caller instantiates this class with (e.g. std::list's, declared in <list>, which this header does not
  // itself include) at the point of instantiation. A qualified std::erase_if(...) call would instead freeze
  // its candidate set to whatever is visible where this header itself is parsed. Mirrors the swap idiom
  // ('using std::swap; swap(a, b);').
  template< class Any_container, class Predicate>
  static constexpr auto call_erase_if(Any_container& container, Predicate& predicate) {
    using std::erase_if;
    return erase_if(container, predicate);
  }

  // Shared by release_surplus() and release_at_least(): 'count == 0' means "all surplus" (see the class
  // comment), otherwise 'count' is taken literally. Returns nullopt when the request exceeds the surplus.
  // release_up_to() does not share this: its 'count' is always literal, and it never refuses.
  constexpr std::optional< size_type> resolve_count(const size_type count) const {
    const size_type surplus = size() - minimal_size;
    const size_type requested = (count == 0) ? surplus : count;
    if (requested > surplus) {
      return std::nullopt;
    }

    return requested;
  }

  // Splits _data into a kept part and a moved-out part of exactly 'to_move' elements; callers validate that
  // 'to_move' does not exceed the surplus before calling. Exception guarantee: with Side::BACK the erased
  // range is the tail, so erasing it after the move cannot throw; with Side::FRONT on a contiguous container,
  // erasing the head shifts the remaining elements down by move-assignment, which may throw for a
  // throwing-move T — the elements already moved out are then lost (basic guarantee only).
  constexpr Container extract(const std::size_t to_move, const Side side) {
    const auto first =
      (side == Side::BACK)
      ? std::ranges::prev(_data.end(), static_cast< std::ptrdiff_t>(to_move))
      : _data.begin();
    const auto last =
      (side == Side::BACK)
      ? _data.end()
      : std::ranges::next(_data.begin(), static_cast< std::ptrdiff_t>(to_move));

    if constexpr (requires { _data.splice(_data.end(), _data, first, last); }) {
      // A list relinks the nodes instead of moving or copying the elements they hold.
      Container moved;
      moved.splice(moved.end(), _data, first, last);
      return moved;
    }
    else if constexpr (std::is_nothrow_move_constructible_v< T> || !std::is_copy_constructible_v< T>) {
      Container moved(std::make_move_iterator(first), std::make_move_iterator(last));
      _data.erase(first, last);
      return moved;
    }
    else {
      // Mirrors std::move_if_noexcept: a throwing move on a copyable T could leave a moved-from husk in
      // _data if the transfer failed partway, so the tail is copied instead.
      Container moved(first, last);
      _data.erase(first, last);
      return moved;
    }
  }

  // The type transform() converts each element to, computed once so both the requires-clause and the body
  // name the same expression.
  template< class Function>
  using Transform_result = std::remove_cvref_t< std::invoke_result_t< Function&, const T&>>;

  // The element type zip() produces when paired with another At_least.
  template< At_least_like Other>
  using Zip_pair = std::tuple< T, typename Other::value_type>;

 public:
  At_least() = delete;

  // Public in signature only: At_least_key's constructor is private and every At_least instantiation is its
  // friend, so nobody outside this class can name a value of this type — making this constructor unreachable
  // from outside. See the comment on details::At_least_key for why the constructor cannot be private itself.
  constexpr explicit At_least(details::At_least_key, Container&& data) : _data(std::move(data)) {}

  constexpr At_least(const At_least&) = default;
  constexpr At_least& operator = (const At_least&) = default;

  // See the class comment: a move would drop the source below MINIMAL_SIZE.
  At_least(At_least&&) = delete;
  At_least& operator = (At_least&&) = delete;

  // Copies a stronger guarantee into a weaker one: an At_least with a higher minimum trivially satisfies a
  // lower one, so no check is needed and the constructor is not explicit. The variadic constructor's
  // At_least_like guard (below) is what keeps this from being shadowed when 'other' is the only argument:
  // without it, an At_least< other_minimal_size, T, Container> lvalue would satisfy the variadic
  // constructor's T(argument) check whenever T can be constructed from it (e.g. T = std::any), wrapping the
  // whole object as one element instead of reaching this constructor.
  template< std::size_t other_minimal_size>
    requires (other_minimal_size > minimal_size)
  constexpr At_least(const At_least< other_minimal_size, T, Container>& other) : _data(other.container()) {}

  constexpr void swap(At_least& other) noexcept(std::is_nothrow_swappable_v< Container>) {
    using std::swap;
    swap(_data, other._data);
  }

  // Swaps in a Container directly, when it is large enough to keep the invariant; unlike swap(At_least&),
  // this can refuse.
  [[nodiscard]] constexpr bool swap(Container& other) noexcept(std::is_nothrow_swappable_v< Container>) {
    if (other.size() < minimal_size) {
      return false;
    }

    using std::swap;
    swap(_data, other);
    return true;
  }

  // A hidden friend so 'using std::swap; swap(a, b);' finds this overload despite At_least having no move
  // constructor for std::swap's own default implementation to use.
  friend constexpr void swap(At_least& left, At_least& right) noexcept(noexcept(left.swap(right))) {
    left.swap(right);
  }

  // Count proven by the pack: sizeof...(Args) >= minimal_size is a compile-time fact. The single-argument
  // guard keeps this template from hijacking the copy constructor for a greedy T (std::any and the like),
  // from silently wrapping a whole Container (or any other range of T) as one element — a range only ever
  // enters through make() or append_range() — and from shadowing the converting constructor above for
  // another At_least instantiation. 'T{argument}' is required in addition to 'T(argument)' purely for its
  // narrowing rules.
  template< class... Args>
    requires
      (sizeof...(Args) >= minimal_size)
      && (
        sizeof...(Args) != 1
        || (
          !(At_least_like< Args> && ...)
          && !(std::same_as< Container, std::remove_cvref_t< Args>> && ...)
          && !(details::Range_of< std::remove_cvref_t< Args>, T> && ...)
        )
      )
      && (
        requires (Args&& argument) {
          T(std::forward< Args>(argument));
          T{std::forward< Args>(argument)};
        }
        && ...
      )
      && requires (Container& container, T&& value) { container.emplace_back(std::move(value)); }
  explicit(sizeof...(Args) == 1) constexpr At_least(Args&&... args) {
    reserve_additional(_data, sizeof...(Args));
    (_data.emplace_back(std::forward< Args>(args)), ...);
  }

  // Container is taken by value so that both an lvalue argument (copied into the parameter) and an rvalue
  // argument (moved into the parameter) bind the same way; the parameter is then moved into the result,
  // which is built in place inside the std::expected through the passkey constructor. On failure the same
  // container is handed back to the caller as the error, so nothing is lost.
  [[nodiscard]] static constexpr std::expected< At_least, Container> make(Container data) {
    if (data.size() < minimal_size) {
      return std::unexpected(std::move(data));
    }

    return std::expected< At_least, Container>(std::in_place, details::At_least_key{}, std::move(data));
  }

  // Built through emplace_back rather than 'Container(std::from_range, range)': container range
  // constructors (P1206) are not yet available in every C++23 standard library this header must build
  // against, so the range is walked once (by fill_from_range()) and each element is constructed in place
  // instead.
  template< std::ranges::input_range Range>
    requires
      std::constructible_from< T, std::ranges::range_reference_t< Range>>
      && requires (Container& container, T&& value) { container.emplace_back(std::move(value)); }
  [[nodiscard]] static constexpr std::expected< At_least, Container> make(std::from_range_t, Range&& range) {
    Container data;
    fill_from_range(data, std::forward< Range>(range));
    return make(std::move(data));
  }

  // 'count' is a template argument, so its being >= minimal_size is a compile-time fact: no optional needed.
  template< std::size_t count>
    requires (count >= minimal_size)
  [[nodiscard]] static constexpr At_least filled(const T& value) {
    return At_least(details::At_least_key{}, Container(count, value));
  }

  // Runtime counterpart of filled(): refuses without constructing anything when 'count' is below
  // minimal_size.
  [[nodiscard]] static constexpr std::optional< At_least> make_filled(const size_type count, const T& value) {
    if (count < minimal_size) {
      return std::nullopt;
    }

    return std::optional< At_least>(std::in_place, details::At_least_key{}, Container(count, value));
  }

  // Key known at compile time: validity is proven, so the access needs no check and cannot throw. Works for
  // any container via std::ranges::next, not just random-access ones.
  template< std::size_t index>
    requires (index < minimal_size)
  [[nodiscard]] constexpr T& at() noexcept {
    return *std::ranges::next(_data.begin(), static_cast< std::ptrdiff_t>(index));
  }

  template< std::size_t index>
    requires (index < minimal_size)
  [[nodiscard]] constexpr const T& at() const noexcept {
    return *std::ranges::next(_data.begin(), static_cast< std::ptrdiff_t>(index));
  }

  // The object is never empty, so these cannot throw.
  [[nodiscard]] constexpr T& front() noexcept {
    return _data.front();
  }

  [[nodiscard]] constexpr const T& front() const noexcept {
    return _data.front();
  }

  [[nodiscard]] constexpr T& back() noexcept {
    return _data.back();
  }

  [[nodiscard]] constexpr const T& back() const noexcept {
    return _data.back();
  }

  // Checked access for a runtime index, mirroring std::vector::at. Only meaningful for a random-access
  // container: a linked list has no O(1) way to validate an arbitrary index before use.
  [[nodiscard]] constexpr T& at(const size_type index) requires std::ranges::random_access_range< Container> {
    if (index >= size()) {
      throw std::out_of_range("At_least: index out of range");
    }

    return _data[index];
  }

  [[nodiscard]] constexpr const T& at(const size_type index) const
    requires std::ranges::random_access_range< Container>
  {
    if (index >= size()) {
      throw std::out_of_range("At_least: index out of range");
    }

    return _data[index];
  }

  // Checked access for a runtime index, returning an empty Optional_reference instead of throwing. Only
  // meaningful for a random-access container, like at(index).
  [[nodiscard]] constexpr Optional_reference< T> try_at(const size_type index) noexcept
    requires std::ranges::random_access_range< Container>
  {
    if (index >= size()) {
      return Optional_reference< T>();
    }

    return Optional_reference< T>(_data[index]);
  }

  [[nodiscard]] constexpr Optional_reference< const T> try_at(const size_type index) const noexcept
    requires std::ranges::random_access_range< Container>
  {
    if (index >= size()) {
      return Optional_reference< const T>();
    }

    return Optional_reference< const T>(_data[index]);
  }

  // Unchecked access, mirroring std::vector::operator[]: an out-of-range index is undefined behaviour.
  // Use at(index) when the index cannot be trusted.
  [[nodiscard]] constexpr T& operator [] (const size_type index) noexcept
    requires std::ranges::random_access_range< Container>
  {
    return _data[index];
  }

  [[nodiscard]] constexpr const T& operator [] (const size_type index) const noexcept
    requires std::ranges::random_access_range< Container>
  {
    return _data[index];
  }

  constexpr iterator begin() noexcept {
    return _data.begin();
  }

  constexpr const_iterator begin() const noexcept {
    return _data.begin();
  }

  constexpr iterator end() noexcept {
    return _data.end();
  }

  constexpr const_iterator end() const noexcept {
    return _data.end();
  }

  [[nodiscard]] constexpr size_type size() const noexcept {
    return _data.size();
  }

  // No empty(): the invariant makes it always false.

  [[nodiscard]] constexpr const Container& container() const noexcept {
    return _data;
  }

  // Contiguous storage returns a std::span so the caller gets a lightweight view without walking iterators;
  // any other bidirectional container returns a std::ranges::subrange instead. An explicit object parameter
  // collapses the const/non-const pair into one template, since the only difference between them is whether
  // 'self' (and therefore the span's element type, via std::span's deduction guide) is const.
  template< class Self>
  [[nodiscard]] constexpr auto head(this Self& self) noexcept {
    if constexpr (std::ranges::contiguous_range< Container>) {
      return std::span(self._data).template first< minimal_size>();
    }
    else {
      return std::ranges::subrange(
        self._data.begin(),
        std::ranges::next(self._data.begin(), static_cast< std::ptrdiff_t>(minimal_size))
      );
    }
  }

  template< class Self>
  [[nodiscard]] constexpr auto surplus(this Self& self) noexcept {
    if constexpr (std::ranges::contiguous_range< Container>) {
      return std::span(self._data).subspan(minimal_size);
    }
    else {
      return std::ranges::subrange(
        std::ranges::next(self._data.begin(), static_cast< std::ptrdiff_t>(minimal_size)),
        self._data.end()
      );
    }
  }

  // Growing operations only ever help the invariant, so none of them return std::optional. Each is
  // constrained on the presence of the matching member in Container, so that vector, deque and list are all
  // usable without an adapter — vector, for instance, has neither *_front member.
  template< class... Args>
    requires requires (Container& container, Args&&... args) {
      container.emplace_back(std::forward< Args>(args)...);
    }
  constexpr T& emplace_back(Args&&... args) {
    return _data.emplace_back(std::forward< Args>(args)...);
  }

  constexpr void push_back(const T& value)
    requires requires (Container& container, const T& value) { container.push_back(value); }
  {
    _data.push_back(value);
  }

  constexpr void push_back(T&& value)
    requires requires (Container& container, T&& value) { container.push_back(std::move(value)); }
  {
    _data.push_back(std::move(value));
  }

  template< class... Args>
    requires requires (Container& container, Args&&... args) {
      container.emplace_front(std::forward< Args>(args)...);
    }
  constexpr T& emplace_front(Args&&... args) {
    return _data.emplace_front(std::forward< Args>(args)...);
  }

  constexpr void push_front(const T& value)
    requires requires (Container& container, const T& value) { container.push_front(value); }
  {
    _data.push_front(value);
  }

  constexpr void push_front(T&& value)
    requires requires (Container& container, T&& value) { container.push_front(std::move(value)); }
  {
    _data.push_front(std::move(value));
  }

  constexpr iterator insert(const_iterator position, const T& value)
    requires requires (Container& container, const_iterator position, const T& value) {
      container.insert(position, value);
    }
  {
    return _data.insert(position, value);
  }

  constexpr iterator insert(const_iterator position, T&& value)
    requires requires (Container& container, const_iterator position, T&& value) {
      container.insert(position, std::move(value));
    }
  {
    return _data.insert(position, std::move(value));
  }

  template< class... Args>
    requires requires (Container& container, const_iterator position, Args&&... args) {
      container.emplace(position, std::forward< Args>(args)...);
    }
  constexpr iterator emplace(const_iterator position, Args&&... args) {
    return _data.emplace(position, std::forward< Args>(args)...);
  }

  // Same reason as make(std::from_range, ...): P1206's range-inserting members aren't available everywhere.
  // Elements of an rvalue range are copied into Container, exactly like std::vector::append_range; pipe the
  // range through std::views::as_rvalue first to move instead.
  template< std::ranges::input_range Range>
    requires
      std::constructible_from< T, std::ranges::range_reference_t< Range>>
      && requires (Container& container, T&& value) { container.emplace_back(std::move(value)); }
  constexpr void append_range(Range&& range) {
    fill_from_range(_data, std::forward< Range>(range));
  }

  // Builds a temporary Container from the range first, then inserts it as a whole: Container::insert(pos,
  // first, last) needs a single pass over matching iterators, which an arbitrary input range does not
  // guarantee to provide twice. Elements are moved out of the temporary, mirroring append_range()'s decision
  // to copy an rvalue container's elements only when the caller explicitly asks to move them.
  template< std::ranges::input_range Range>
    requires
      std::constructible_from< T, std::ranges::range_reference_t< Range>>
      && requires (Container& container, T&& value) { container.emplace_back(std::move(value)); }
      && requires (
        Container& container,
        const_iterator position,
        std::move_iterator< iterator> first,
        std::move_iterator< iterator> last
      ) {
        container.insert(position, first, last);
      }
  constexpr iterator insert_range(const const_iterator position, Range&& range) {
    Container temporary;
    fill_from_range(temporary, std::forward< Range>(range));

    return _data.insert(
      position,
      std::make_move_iterator(temporary.begin()),
      std::make_move_iterator(temporary.end())
    );
  }

  // Constrained on both what insert_range() needs and on Container having emplace_front; the latter serves
  // only to exclude vector (which has no O(1) front insertion) from this convenience wrapper — insert_range()
  // itself remains available for vector at an explicit position.
  template< std::ranges::input_range Range>
    requires
      std::constructible_from< T, std::ranges::range_reference_t< Range>>
      && requires (Container& container, T&& value) {
        container.emplace_back(std::move(value));
        container.emplace_front(std::move(value));
      }
      && requires (
        Container& container,
        const_iterator position,
        std::move_iterator< iterator> first,
        std::move_iterator< iterator> last
      ) {
        container.insert(position, first, last);
      }
  constexpr void prepend_range(Range&& range) {
    insert_range(_data.begin(), std::forward< Range>(range));
  }

  // Applied to every element in order; the element count is preserved, so no optional is needed. Built
  // through the shared passkey since the result is a different At_least< minimal_size, U, ...> instantiation
  // than 'this'.
  template< template< class...> class Result_container = std::vector, class Function>
    requires
      std::regular_invocable< Function&, const T&>
      && (!std::is_void_v< Transform_result< Function>>)
      && details::Suitable_container< Result_container< Transform_result< Function>>>
      && requires (Result_container< Transform_result< Function>>& container, Transform_result< Function>&& value) {
        container.emplace_back(std::move(value));
      }
  [[nodiscard]] constexpr auto transform(Function function) const {
    using U = Transform_result< Function>;

    return At_least< minimal_size, U, Result_container< U>>(
      details::At_least_key{},
      _data | std::views::transform(std::ref(function)) | std::ranges::to< Result_container< U>>()
    );
  }

  // Pairs elements from both sources up to the shorter one; the result's minimum is proven by both operands'
  // minima, so no optional is needed.
  template< template< class...> class Result_container = std::vector, At_least_like Other>
    requires
      std::copy_constructible< T>
      && std::copy_constructible< typename Other::value_type>
      && details::Suitable_container< Result_container< Zip_pair< Other>>>
      && requires (Result_container< Zip_pair< Other>>& container, Zip_pair< Other>&& value) {
        container.emplace_back(std::move(value));
      }
  [[nodiscard]] constexpr auto zip(const Other& other) const {
    using Pair = Zip_pair< Other>;

    return At_least< std::min(minimal_size, Other::MINIMAL_SIZE), Pair, Result_container< Pair>>(
      details::At_least_key{},
      std::views::zip(_data, other.container()) | std::ranges::to< Result_container< Pair>>()
    );
  }

  // Shrinking operations return std::optional and leave the object unchanged on refusal, since removing an
  // element can push the count below MINIMAL_SIZE.
  [[nodiscard]] constexpr std::optional< T> pop_back()
    requires requires (Container& container) { container.pop_back(); }
  {
    if (size() == minimal_size) {
      return std::nullopt;
    }

    T value(std::move(_data.back()));
    _data.pop_back();
    return value;
  }

  [[nodiscard]] constexpr std::optional< T> pop_front()
    requires requires (Container& container) { container.pop_front(); }
  {
    if (size() == minimal_size) {
      return std::nullopt;
    }

    T value(std::move(_data.front()));
    _data.pop_front();
    return value;
  }

  [[nodiscard]] constexpr std::optional< iterator> erase(const_iterator position)
    requires requires (Container& container, const_iterator position) { container.erase(position); }
  {
    if (size() == minimal_size) {
      return std::nullopt;
    }

    return _data.erase(position);
  }

  [[nodiscard]] constexpr std::optional< iterator> erase(const_iterator first, const_iterator last)
    requires requires (Container& container, const_iterator first, const_iterator last) {
      container.erase(first, last);
    }
  {
    const auto removed_count = static_cast< std::size_t>(std::ranges::distance(first, last));
    if (removed_count > size() - minimal_size) {
      return std::nullopt;
    }

    return _data.erase(first, last);
  }

  // Evaluates the predicate exactly once per element, in order, recording each verdict into 'flags' instead
  // of evaluating it a second time when erasing: a predicate that is not equality-preserving across two
  // evaluations (e.g. depending on call count or external state) could otherwise remove more elements on
  // erase_if's own pass than counted on a separate counting pass, dropping the size below MINIMAL_SIZE
  // without the refusal check ever seeing it. std::vector<T>::erase_if / std::list<T>::remove_if and the
  // like visit each element exactly once, in order, so replaying the recorded verdicts by a running index
  // reproduces exactly the counted matches.
  template< class Predicate>
    requires
      std::predicate< Predicate&, const T&>
      && requires (Container& container, Predicate& predicate) { call_erase_if(container, predicate); }
  [[nodiscard]] constexpr std::optional< size_type> erase_if(Predicate predicate) {
    std::vector< bool> flags;
    flags.reserve(size());
    size_type matches = 0;
    for (const T& element : _data) {
      const bool matched = std::invoke(predicate, element);
      flags.push_back(matched);
      matches += matched ? 1 : 0;
    }

    if (size() - matches < minimal_size) {
      return std::nullopt;
    }

    auto is_marked = [&flags, index = std::size_t{0}](const T&) mutable { return flags[index++]; };
    return call_erase_if(_data, is_marked);
  }

  [[nodiscard]] constexpr std::optional< size_type> resize(const size_type new_size)
    requires
      requires (Container& container, size_type count) { container.resize(count); }
      && std::default_initializable< T>
  {
    if (new_size < minimal_size) {
      return std::nullopt;
    }

    const size_type previous_size = size();
    _data.resize(new_size);
    return previous_size;
  }

  [[nodiscard]] constexpr std::optional< size_type> resize(const size_type new_size, const T& value)
    requires requires (Container& container, size_type count, const T& value) {
      container.resize(count, value);
    }
  {
    if (new_size < minimal_size) {
      return std::nullopt;
    }

    const size_type previous_size = size();
    _data.resize(new_size, value);
    return previous_size;
  }

  // Moves the requested elements out and returns them as a plain Container. 'count == 0' asks for all
  // surplus; refuses without touching this object when 'count' exceeds the surplus. See the class comment
  // for what 'side' selects.
  [[nodiscard]] constexpr std::optional< Container> release_surplus(
    const size_type count = 0,
    const Side side = Side::BACK
  ) {
    const std::optional< size_type> to_move = resolve_count(count);
    if (!to_move) {
      return std::nullopt;
    }

    return extract(*to_move, side);
  }

  // Same transfer as release_surplus(), but the moved-out part becomes a new At_least of
  // 'result_minimal_size' instead of a plain Container, built through the shared passkey. Refuses in
  // addition when the moved-out part would hold fewer than 'result_minimal_size' elements. Constrained to a
  // positive result_minimal_size so that release_at_least< 0>() is rejected here rather than instantiating
  // At_least< 0, ...>, which would fail its own static_assert instead of failing gracefully.
  template< std::size_t result_minimal_size = minimal_size>
    requires (result_minimal_size > 0)
  [[nodiscard]] constexpr std::optional< At_least< result_minimal_size, T, Container>> release_at_least(
    const size_type count = 0,
    const Side side = Side::BACK
  ) {
    const std::optional< size_type> to_move = resolve_count(count);
    if (!to_move || *to_move < result_minimal_size) {
      return std::nullopt;
    }

    return std::optional< At_least< result_minimal_size, T, Container>>(
      std::in_place,
      details::At_least_key{},
      extract(*to_move, side)
    );
  }

  // Unlike release_surplus(), never refuses: moves std::min(count, surplus) elements out. 'count' is taken
  // literally here (0 moves nothing), unlike release_surplus()'s 'count == 0' meaning "all surplus".
  [[nodiscard]] constexpr Container release_up_to(const size_type count, const Side side = Side::BACK) {
    const size_type surplus = size() - minimal_size;
    return extract(std::min(count, surplus), side);
  }

  // Forward to the underlying container. Written as ordinary members (not '= default') so their bodies
  // instantiate lazily, only where a comparison is actually used. operator<=> only demands what Container's
  // own synthesized three-way order demands (operator<), not the stronger std::three_way_comparable.
  [[nodiscard]] constexpr bool operator == (const At_least& other) const requires std::equality_comparable< T> {
    return _data == other._data;
  }

  [[nodiscard]] constexpr auto operator <=> (const At_least& other) const
    requires details::Synthesized_three_way_comparable< T>
  {
    return _data <=> other._data;
  }
};

template< class T, class Container = std::vector< T>>
using Nonempty = At_least< 1, T, Container>;

} // namespace core::utils
