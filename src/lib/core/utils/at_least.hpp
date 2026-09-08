#pragma once

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>


namespace core::utils::details {

// std::vector's own operator<=> only demands operator< from its element type (synthesized three-way order),
// not std::three_way_comparable; requiring the stronger concept here would reject a T that Container itself
// happily orders.
template< class T>
concept Synth_three_way_comparable =
  std::three_way_comparable< T>
  || requires (const T& left, const T& right) {
    { left < right } -> std::convertible_to< bool>;
  };

} // namespace core::utils::details

namespace core::utils {

/// Wraps a Container so that at least MINIMAL_SIZE elements are present at every moment of the object's
/// life. The invariant is enforced only through the interface: there is no default constructor, and every
/// operation that could drop the element count below MINIMAL_SIZE refuses instead of running, leaving the
/// object unchanged.
///
/// release_surplus() and release_at_least() both keep the first MINIMAL_SIZE elements in this object and
/// move the tail out; 'count == 0' means "all surplus", i.e. everything past the first MINIMAL_SIZE
/// elements. release_surplus() hands the tail back as a plain Container; release_at_least() wraps the same
/// tail as a new At_least, so it refuses in addition when the tail itself would hold fewer than
/// MINIMAL_SIZE elements.
///
/// The move constructor and move assignment are deleted: moving _data out would leave the source below
/// MINIMAL_SIZE, which the public interface must never allow — a moved-from container is only guaranteed to
/// be valid for its own type, not "at least MINIMAL_SIZE" for this wrapper. A whole container is taken in
/// only through make(), which checks its size before it ever reaches _data; elements move out only through
/// release_surplus() / release_at_least(), which keep MINIMAL_SIZE elements behind.
///
/// at(index) mirrors std::vector::at and is the only member that throws; every other fallible operation
/// reports refusal through std::optional instead. The iterator-taking members (insert, emplace, erase) carry
/// the same preconditions as the matching Container member, and the iterators they return are invalidated
/// the same way Container's would be.
template< std::size_t minimal_size, class T, class Container = std::vector< T>>
  requires std::ranges::bidirectional_range< Container> && std::ranges::sized_range< Container>
class At_least {
  static_assert(minimal_size > 0, "At_least< 0, ...> is just the container: use it directly");
  static_assert(std::same_as< typename Container::value_type, T>, "Container::value_type must match T");

  Container _data;

  // A passkey: nobody outside this class can produce a Checked, since both the type and its default
  // constructor are private (an empty braced argument no longer suffices once the constructor itself is
  // private). Only make() and release_at_least() ever produce one, after the size check has already passed,
  // so reaching the constructor that takes it is itself the proof that the invariant holds. That constructor
  // has to stay public rather than private-plus-friending std::optional: std::optional's in-place constructor
  // is gated by std::is_constructible, and that check does not honour a friend declaration naming a specific
  // std::optional specialization.
  struct Checked {
   private:
    friend At_least;
    Checked() = default;
  };

  // Shared by release_surplus() and release_at_least(): moves the last 'to_move' elements (all surplus when
  // 'count == 0') out of _data into a fresh Container and erases them from _data. Refuses without touching
  // _data when the request does not fit the surplus. The split point is found by walking backward from
  // end(), which only needs a bidirectional range.
  std::optional< Container> extract_tail(const std::size_t count) {
    const std::size_t surplus = size() - minimal_size;
    const std::size_t to_move = (count == 0) ? surplus : count;
    if (to_move > surplus) {
      return std::nullopt;
    }

    const auto split_point = std::ranges::prev(_data.end(), static_cast< std::ptrdiff_t>(to_move));
    Container moved;

    if constexpr (requires { moved.splice(moved.end(), _data, split_point, _data.end()); }) {
      // A list relinks the nodes instead of moving or copying the elements they hold.
      moved.splice(moved.end(), _data, split_point, _data.end());
      return moved;
    }
    else if constexpr (std::is_nothrow_move_constructible_v< T> || !std::is_copy_constructible_v< T>) {
      moved = Container(std::make_move_iterator(split_point), std::make_move_iterator(_data.end()));
    }
    else {
      // Mirrors std::move_if_noexcept: a throwing move on a copyable T could leave a moved-from husk in
      // _data if the transfer failed partway, so the tail is copied instead.
      moved = Container(split_point, _data.end());
    }

    _data.erase(split_point, _data.end());
    return moved;
  }

  // Grows Container's capacity ahead of inserting 'count' more elements, when Container supports reserve()
  // at all (std::list does not). Takes the container explicitly so both an instance's _data and a factory's
  // local Container can share this logic.
  static void reserve_additional(Container& container, const std::size_t count) {
    if constexpr (requires { container.reserve(count); }) {
      container.reserve(container.size() + count);
    }
  }

 public:
  static constexpr std::size_t MINIMAL_SIZE = minimal_size;
  using value_type = typename Container::value_type;
  using iterator = typename Container::iterator;
  using const_iterator = typename Container::const_iterator;
  using size_type = typename Container::size_type;

  At_least() = delete;

  // Public in signature only: the Checked parameter type is private, so this constructor is unreachable
  // from outside the class. See the comment on Checked for why it cannot be private itself.
  explicit At_least(Checked, Container&& data) : _data(std::move(data)) {}

  At_least(const At_least&) = default;
  At_least& operator = (const At_least&) = default;

  // See the class comment: a move would drop the source below MINIMAL_SIZE.
  At_least(At_least&&) = delete;
  At_least& operator = (At_least&&) = delete;

  void swap(At_least& other) noexcept(std::is_nothrow_swappable_v< Container>) {
    using std::swap;
    swap(_data, other._data);
  }

  // A hidden friend so 'using std::swap; swap(a, b);' finds this overload despite At_least having no move
  // constructor for std::swap's own default implementation to use.
  friend void swap(At_least& left, At_least& right) noexcept(noexcept(left.swap(right))) {
    left.swap(right);
  }

  // Count proven by the pack: sizeof...(Args) >= minimal_size is a compile-time fact. The single-argument
  // guard keeps this template from hijacking the copy constructor for a greedy T (std::any and the like) and
  // from silently wrapping a whole Container as one element — a Container only ever enters through make().
  // 'T{argument}' is required in addition to 'T(argument)' purely for its narrowing rules.
  template< class... Args>
    requires
      (sizeof...(Args) >= minimal_size)
      && (
        sizeof...(Args) != 1
        || (
          !(std::same_as< At_least, std::remove_cvref_t< Args>> && ...)
          && !(std::same_as< Container, std::remove_cvref_t< Args>> && ...)
        )
      )
      && (requires (Args&& argument) {
            T(std::forward< Args>(argument));
            T{std::forward< Args>(argument)};
          } && ...)
  explicit(sizeof...(Args) == 1) At_least(Args&&... args) {
    reserve_additional(_data, sizeof...(Args));
    (_data.emplace_back(std::forward< Args>(args)), ...);
  }

  // Container is taken by value so that both an lvalue argument (copied into the parameter) and an rvalue
  // argument (moved into the parameter) bind the same way; the parameter is then moved into the result,
  // which is built in place inside the optional through the passkey constructor.
  static std::optional< At_least> make(Container data) {
    if (data.size() < minimal_size) {
      return std::nullopt;
    }

    return std::optional< At_least>(std::in_place, Checked{}, std::move(data));
  }

  // Built through emplace_back rather than 'Container(std::from_range, range)': container range
  // constructors (P1206) are not yet available in every C++23 standard library this header must build
  // against, so the range is walked once and each element is constructed in place instead.
  template< std::ranges::input_range Range>
    requires std::constructible_from< T, std::ranges::range_reference_t< Range>>
  static std::optional< At_least> make(std::from_range_t, Range&& range) {
    Container data;
    if constexpr (std::ranges::sized_range< Range>) {
      reserve_additional(data, static_cast< std::size_t>(std::ranges::size(range)));
    }

    std::ranges::for_each(range, [&data](auto&& value) {
      data.emplace_back(std::forward< decltype(value)>(value));
    });

    return make(std::move(data));
  }

  // Key known at compile time: validity is proven, so the access needs no check and cannot throw. Works for
  // any container via std::ranges::next, not just random-access ones.
  template< std::size_t index>
    requires (index < minimal_size)
  T& at() noexcept {
    return *std::ranges::next(_data.begin(), static_cast< std::ptrdiff_t>(index));
  }

  template< std::size_t index>
    requires (index < minimal_size)
  const T& at() const noexcept {
    return *std::ranges::next(_data.begin(), static_cast< std::ptrdiff_t>(index));
  }

  // The object is never empty, so these cannot throw.
  T& front() noexcept {
    return _data.front();
  }

  const T& front() const noexcept {
    return _data.front();
  }

  T& back() noexcept {
    return _data.back();
  }

  const T& back() const noexcept {
    return _data.back();
  }

  // Checked access for a runtime index, mirroring std::vector::at. Only meaningful for a random-access
  // container: a linked list has no O(1) way to validate an arbitrary index before use.
  T& at(const std::size_t index) requires std::ranges::random_access_range< Container> {
    if (index >= size()) {
      throw std::out_of_range("At_least: index out of range");
    }

    return _data[index];
  }

  const T& at(const std::size_t index) const requires std::ranges::random_access_range< Container> {
    if (index >= size()) {
      throw std::out_of_range("At_least: index out of range");
    }

    return _data[index];
  }

  // Unchecked access, mirroring std::vector::operator[]: an out-of-range index is undefined behaviour.
  // Use at(index) when the index cannot be trusted.
  T& operator [] (const std::size_t index) noexcept
    requires std::ranges::random_access_range< Container>
  {
    return _data[index];
  }

  const T& operator [] (const std::size_t index) const noexcept
    requires std::ranges::random_access_range< Container>
  {
    return _data[index];
  }

  iterator begin() noexcept {
    return _data.begin();
  }

  const_iterator begin() const noexcept {
    return _data.begin();
  }

  iterator end() noexcept {
    return _data.end();
  }

  const_iterator end() const noexcept {
    return _data.end();
  }

  size_type size() const noexcept {
    return _data.size();
  }

  // No empty(): the invariant makes it always false.

  const Container& container() const noexcept {
    return _data;
  }

  // Growing operations only ever help the invariant, so none of them return std::optional. Each is
  // constrained on the presence of the matching member in Container, so that vector, deque and list are all
  // usable without an adapter — vector, for instance, has neither *_front member.
  template< class... Args>
    requires requires (Container& container, Args&&... args) {
      container.emplace_back(std::forward< Args>(args)...);
    }
  T& emplace_back(Args&&... args) {
    return _data.emplace_back(std::forward< Args>(args)...);
  }

  void push_back(const T& value)
    requires requires (Container& container, const T& value) { container.push_back(value); }
  {
    _data.push_back(value);
  }

  void push_back(T&& value)
    requires requires (Container& container, T&& value) { container.push_back(std::move(value)); }
  {
    _data.push_back(std::move(value));
  }

  template< class... Args>
    requires requires (Container& container, Args&&... args) {
      container.emplace_front(std::forward< Args>(args)...);
    }
  T& emplace_front(Args&&... args) {
    return _data.emplace_front(std::forward< Args>(args)...);
  }

  void push_front(const T& value)
    requires requires (Container& container, const T& value) { container.push_front(value); }
  {
    _data.push_front(value);
  }

  void push_front(T&& value)
    requires requires (Container& container, T&& value) { container.push_front(std::move(value)); }
  {
    _data.push_front(std::move(value));
  }

  iterator insert(const_iterator position, const T& value)
    requires requires (Container& container, const_iterator position, const T& value) {
      container.insert(position, value);
    }
  {
    return _data.insert(position, value);
  }

  iterator insert(const_iterator position, T&& value)
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
  iterator emplace(const_iterator position, Args&&... args) {
    return _data.emplace(position, std::forward< Args>(args)...);
  }

  // Same reason as make(std::from_range, ...): P1206's range-inserting members aren't available everywhere.
  template< std::ranges::input_range Range>
    requires requires (Container& container, std::ranges::range_reference_t< Range> value) {
      container.emplace_back(std::forward< std::ranges::range_reference_t< Range>>(value));
    }
  void append_range(Range&& range) {
    if constexpr (std::ranges::sized_range< Range>) {
      reserve_additional(_data, static_cast< std::size_t>(std::ranges::size(range)));
    }

    std::ranges::for_each(range, [this](auto&& value) {
      _data.emplace_back(std::forward< decltype(value)>(value));
    });
  }

  // Shrinking operations return std::optional and leave the object unchanged on refusal, since removing an
  // element can push the count below MINIMAL_SIZE.
  std::optional< T> pop_back() requires requires (Container& container) { container.pop_back(); } {
    if (size() == minimal_size) {
      return std::nullopt;
    }

    T value(std::move(_data.back()));
    _data.pop_back();
    return value;
  }

  std::optional< T> pop_front() requires requires (Container& container) { container.pop_front(); } {
    if (size() == minimal_size) {
      return std::nullopt;
    }

    T value(std::move(_data.front()));
    _data.pop_front();
    return value;
  }

  std::optional< iterator> erase(const_iterator position)
    requires requires (Container& container, const_iterator position) { container.erase(position); }
  {
    if (size() == minimal_size) {
      return std::nullopt;
    }

    return _data.erase(position);
  }

  std::optional< iterator> erase(const_iterator first, const_iterator last)
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

  std::optional< std::size_t> resize(const std::size_t new_size)
    requires
      requires (Container& container, std::size_t count) { container.resize(count); }
      && std::default_initializable< T>
  {
    if (new_size < minimal_size) {
      return std::nullopt;
    }

    const std::size_t previous_size = size();
    _data.resize(new_size);
    return previous_size;
  }

  // Moves the tail out and returns it as a plain Container. 'count == 0' asks for all surplus; refuses
  // without touching this object when 'count' exceeds the surplus.
  std::optional< Container> release_surplus(const std::size_t count = 0) {
    return extract_tail(count);
  }

  // Same transfer as release_surplus(), but the tail becomes a new At_least instead of a plain Container.
  // Refuses in addition when the tail would hold fewer than MINIMAL_SIZE elements — 'requested' is never
  // actually 0 by the time extract_tail() runs, since minimal_size > 0 already rejects that case above.
  std::optional< At_least> release_at_least(const std::size_t count = 0) {
    const std::size_t requested = (count == 0) ? (size() - minimal_size) : count;
    if (requested < minimal_size) {
      return std::nullopt;
    }

    std::optional< Container> moved = extract_tail(requested);
    if (!moved) {
      return std::nullopt;
    }

    return std::optional< At_least>(std::in_place, Checked{}, std::move(*moved));
  }

  // Forward to the underlying container. Written as ordinary members (not '= default') so their bodies
  // instantiate lazily, only where a comparison is actually used. operator<=> only demands what Container's
  // own synthesized three-way order demands (operator<), not the stronger std::three_way_comparable.
  bool operator == (const At_least& other) const requires std::equality_comparable< T> {
    return _data == other._data;
  }

  auto operator <=> (const At_least& other) const requires details::Synth_three_way_comparable< T> {
    return _data <=> other._data;
  }
};

template< class T, class Container = std::vector< T>>
using Nonempty = At_least< 1, T, Container>;

} // namespace core::utils
