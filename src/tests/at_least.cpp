#include "core/utils/At_least.hpp"

#include <gtest/gtest.h>

#include <any>
#include <concepts>
#include <deque>
#include <list>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace core::utils;

// Asserts that 'range' holds exactly 'expected', element by element and by count, so a failure names the
// differing slot instead of only "not equal" — used in place of a manual iterator walk when the container
// (e.g. std::list) has no operator[].
template< std::ranges::input_range Range, class T>
void expect_elements(const Range& range, std::initializer_list< T> expected) {
  EXPECT_EQ(static_cast< std::size_t>(std::ranges::distance(range)), expected.size());

  std::size_t index = 0;
  for (const auto& value : range) {
    if (index >= expected.size()) {
      break;
    }

    EXPECT_EQ(value, std::data(expected)[index]) << "at index " << index;
    ++index;
  }
}

// ---- compile-time refusals ----
static_assert(!std::is_default_constructible_v< At_least< 3, int>>);
static_assert(!std::is_move_constructible_v< At_least< 3, int>>);
static_assert(!std::is_move_assignable_v< At_least< 3, int>>);
static_assert(std::is_copy_constructible_v< At_least< 3, int>>);
static_assert(!std::is_constructible_v< At_least< 2, int>, int>);

// Single-argument construction is explicit; a bare int must not implicitly convert.
static_assert(!std::is_convertible_v< int, Nonempty< int>>);
static_assert(std::is_constructible_v< Nonempty< int>, int>);

// Narrowing is rejected the same way enums::Array rejects it: T{argument} is required in addition to T(argument).
static_assert(!std::is_constructible_v< Nonempty< int>, double>);

// A Container is never silently wrapped as one element through the variadic constructor: make(Container) is
// the only path in for a whole container.
static_assert(!std::is_constructible_v< Nonempty< std::any>, std::vector< std::any>>);
static_assert(std::is_constructible_v< Nonempty< std::any>, std::any>);

// Nor is any other range of T, even with a different Container type than At_least's own: a
// std::vector< std::any> handed to At_least< 1, std::any, std::deque< std::any>> must not be wrapped as a
// single element (std::any's greedy constructor would otherwise happily accept the whole vector).
static_assert(!std::is_constructible_v< At_least< 1, std::any, std::deque< std::any>>, std::vector< std::any>>);

// A container whose reference type is a proxy (std::vector< bool>) cannot back At_least, since Optional_
// reference, head()/surplus() and the passkey constructor all assume T& is a real reference into storage.
// The exclusion is a template constraint (not a static_assert in the body) precisely so it is SFINAE-probeable
// like this, instead of hard-failing whenever such an At_least is merely named.
template< class T, class Container>
inline constexpr bool at_least_instantiable = requires { typename At_least< 1, T, Container>; };
static_assert(!at_least_instantiable< bool, std::vector< bool>>);
static_assert(at_least_instantiable< int, std::vector< int>>);

// at< index>() with an out-of-range index does not compile: the helper below turns that into a checkable
// fact. The index has to be a genuine template parameter of the helper itself (not a literal matching an
// already-concrete instantiation), otherwise this compiler does not defer the constraint check as SFINAE.
template< std::size_t index>
inline constexpr bool at_index_compiles_for_at_least_2 = requires (At_least< 2, int>& instance) {
  instance.template at< index>();
};
static_assert(!at_index_compiles_for_at_least_2< 2>);

// Instantiating At_least< 0, ...> is a static_assert, not something tested at run time.

// The passkey type is unreachable from outside even through an empty braced argument: {} no longer
// default-constructs Checked once its own default constructor is private.
template< class Least>
inline constexpr bool bypasses_passkey = requires { Least({}, std::vector< int>{}); };
static_assert(!bypasses_passkey< At_least< 3, int>>);

TEST(AtLeast, VariadicConstructsExactlyMinimalSize) {
  const At_least< 3, int> values(1, 2, 3);

  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values.at< 2>(), 3);
}

TEST(AtLeast, VariadicConstructsMoreThanMinimalSize) {
  const At_least< 3, int> values(1, 2, 3, 4, 5);

  ASSERT_EQ(values.size(), 5u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values.at< 2>(), 3);
  EXPECT_EQ(values[3], 4);
  EXPECT_EQ(values[4], 5);
}

// A greedy converting T (std::any) plus MINIMAL_SIZE == 1 makes the variadic constructor call-compatible
// with copying; the guard keeps the copy constructor reachable regardless.
TEST(AtLeast, CopyConstructionIsNotHijackedByVariadicConstructor) {
  const Nonempty< std::any> source(std::any(42));

  const Nonempty< std::any> copy(source);
  const Nonempty< std::any> braced_copy{source};

  ASSERT_TRUE(copy.front().has_value());
  EXPECT_EQ(std::any_cast< int>(copy.front()), 42);
  EXPECT_EQ(std::any_cast< int>(braced_copy.front()), 42);
}

// The exclusion pinned by the static_asserts above, exercised through make() instead.
TEST(AtLeast, MakeAcceptsAWholeContainerThatTheConstructorRejects) {
  const auto result = Nonempty< std::any>::make(std::vector< std::any>{std::any(1)});

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(std::any_cast< int>(result->front()), 1);
}

TEST(AtLeast, MakeRefusesTooFewElements) {
  const auto result = At_least< 3, int>::make(std::vector< int>{1, 2});

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 2u);
  EXPECT_EQ(result.error()[0], 1);
  EXPECT_EQ(result.error()[1], 2);
}

// The container handed back through error() is the very one the caller passed in, not a fresh copy: a
// move-only element's addresses stay the same across the refusal.
TEST(AtLeast, MakeFailureReturnsTheSameContainerByAddress) {
  std::vector< std::unique_ptr< int>> source;
  source.push_back(std::make_unique< int>(1));
  source.push_back(std::make_unique< int>(2));

  const int* const first_address = source[0].get();
  const int* const second_address = source[1].get();

  auto result = At_least< 3, std::unique_ptr< int>>::make(std::move(source));

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 2u);
  EXPECT_EQ(result.error()[0].get(), first_address);
  EXPECT_EQ(result.error()[1].get(), second_address);
}

TEST(AtLeast, MakeAcceptsExactlyMinimalSize) {
  const auto result = At_least< 3, int>::make(std::vector< int>{1, 2, 3});

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3u);
  EXPECT_EQ(result->at< 0>(), 1);
  EXPECT_EQ(result->at< 1>(), 2);
  EXPECT_EQ(result->at< 2>(), 3);
}

TEST(AtLeast, MakeAcceptsMoreThanMinimalSize) {
  const auto result = At_least< 3, int>::make(std::vector< int>{1, 2, 3, 4});

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 4u);
  EXPECT_EQ((*result)[0], 1);
  EXPECT_EQ((*result)[1], 2);
  EXPECT_EQ((*result)[2], 3);
  EXPECT_EQ((*result)[3], 4);
}

// A move-only element proves make() moves the container in: this could not compile at all if it copied.
// Recording each element's address beforehand and finding the same addresses afterward proves it is the
// same allocation that moved, not merely a compiling call.
TEST(AtLeast, MakeMovesTheContainerRatherThanCopyingIt) {
  std::vector< std::unique_ptr< int>> source;
  source.push_back(std::make_unique< int>(1));
  source.push_back(std::make_unique< int>(2));
  source.push_back(std::make_unique< int>(3));

  const int* const first_address = source[0].get();
  const int* const second_address = source[1].get();
  const int* const third_address = source[2].get();

  auto result = At_least< 3, std::unique_ptr< int>>::make(std::move(source));

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3u);
  EXPECT_EQ(result->at< 0>().get(), first_address);
  EXPECT_EQ(result->at< 1>().get(), second_address);
  EXPECT_EQ(result->at< 2>().get(), third_address);
  EXPECT_EQ(*result->at< 0>(), 1);
  EXPECT_EQ(*result->at< 1>(), 2);
  EXPECT_EQ(*result->at< 2>(), 3);
}

TEST(AtLeast, MakeFromRangeRefusesTooFewElements) {
  const auto result = At_least< 3, int>::make(std::from_range, std::vector< int>{1, 2});

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 2u);
  EXPECT_EQ(result.error()[0], 1);
  EXPECT_EQ(result.error()[1], 2);
}

TEST(AtLeast, MakeFromRangeAcceptsExactlyMinimalSize) {
  const auto result = At_least< 3, int>::make(std::from_range, std::vector< int>{1, 2, 3});

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3u);
  EXPECT_EQ(result->at< 0>(), 1);
  EXPECT_EQ(result->at< 1>(), 2);
  EXPECT_EQ(result->at< 2>(), 3);
}

TEST(AtLeast, MakeFromRangeAcceptsSinglePassInputRangeWithEnoughElements) {
  std::istringstream stream("1 2 3");
  const auto result = At_least< 3, int>::make(std::from_range, std::views::istream< int>(stream));

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3u);
  EXPECT_EQ(result->at< 0>(), 1);
  EXPECT_EQ(result->at< 1>(), 2);
  EXPECT_EQ(result->at< 2>(), 3);
}

TEST(AtLeast, MakeFromRangeRefusesSinglePassInputRangeWithTooFewElements) {
  std::istringstream stream("1 2");
  const auto result = At_least< 3, int>::make(std::from_range, std::views::istream< int>(stream));

  ASSERT_FALSE(result.has_value());
  ASSERT_EQ(result.error().size(), 2u);
  EXPECT_EQ(result.error()[0], 1);
  EXPECT_EQ(result.error()[1], 2);
}

TEST(AtLeast, MakeFromRangeAcceptsMoreThanMinimalSize) {
  const auto result = At_least< 3, int>::make(std::from_range, std::views::iota(1, 5));

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 4u);
  EXPECT_EQ((*result)[0], 1);
  EXPECT_EQ((*result)[1], 2);
  EXPECT_EQ((*result)[2], 3);
  EXPECT_EQ((*result)[3], 4);
}

TEST(AtLeast, ReleaseSurplusDefaultTakesEverythingPastMinimalSize) {
  At_least< 3, int> values(1, 2, 3, 4, 5);

  const auto surplus = values.release_surplus();

  ASSERT_TRUE(surplus.has_value());
  ASSERT_EQ(surplus->size(), 2u);
  EXPECT_EQ((*surplus)[0], 4);
  EXPECT_EQ((*surplus)[1], 5);

  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values.at< 2>(), 3);
}

TEST(AtLeast, ReleaseSurplusExplicitCountWithinLimits) {
  At_least< 3, int> values(1, 2, 3, 4, 5);

  const auto surplus = values.release_surplus(1);

  ASSERT_TRUE(surplus.has_value());
  ASSERT_EQ(surplus->size(), 1u);
  EXPECT_EQ((*surplus)[0], 5);

  ASSERT_EQ(values.size(), 4u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values.at< 2>(), 3);
  EXPECT_EQ(values[3], 4);
}

TEST(AtLeast, ReleaseSurplusRefusesCountExceedingSurplus) {
  At_least< 3, int> values(1, 2, 3, 4, 5);

  const auto surplus = values.release_surplus(3);

  EXPECT_FALSE(surplus.has_value());
  ASSERT_EQ(values.size(), 5u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values.at< 2>(), 3);
  EXPECT_EQ(values[3], 4);
  EXPECT_EQ(values[4], 5);
}

TEST(AtLeast, ReleaseSurplusWithNoSurplusReturnsAnEmptyContainer) {
  At_least< 3, int> values(1, 2, 3);

  const auto surplus = values.release_surplus();

  ASSERT_TRUE(surplus.has_value());
  EXPECT_TRUE(surplus->empty());

  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values.at< 2>(), 3);
}

TEST(AtLeast, ReleaseSurplusWorksWithList) {
  At_least< 2, int, std::list< int>> values(1, 2, 3, 4);

  const auto surplus = values.release_surplus();

  ASSERT_TRUE(surplus.has_value());
  ASSERT_EQ(surplus->size(), 2u);
  auto surplus_iterator = surplus->begin();
  EXPECT_EQ(*surplus_iterator, 3);
  ++surplus_iterator;
  EXPECT_EQ(*surplus_iterator, 4);

  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
}

TEST(AtLeast, ReleaseSurplusWorksWithDeque) {
  At_least< 2, int, std::deque< int>> values(1, 2, 3, 4);

  const auto surplus = values.release_surplus(1);

  ASSERT_TRUE(surplus.has_value());
  ASSERT_EQ(surplus->size(), 1u);
  EXPECT_EQ((*surplus)[0], 4);

  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(values[2], 3);
}

TEST(AtLeast, ReleaseAtLeastDefaultSplitsIntoTwoValidObjects) {
  At_least< 2, int> values(1, 2, 3, 4);

  const auto tail = values.release_at_least();

  ASSERT_TRUE(tail.has_value());
  ASSERT_EQ(tail->size(), 2u);
  EXPECT_EQ(tail->at< 0>(), 3);
  EXPECT_EQ(tail->at< 1>(), 4);

  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
}

TEST(AtLeast, ReleaseAtLeastExplicitCount) {
  At_least< 2, int> values(1, 2, 3, 4, 5, 6);

  const auto tail = values.release_at_least(3);

  ASSERT_TRUE(tail.has_value());
  ASSERT_EQ(tail->size(), 3u);
  EXPECT_EQ(tail->at< 0>(), 4);
  EXPECT_EQ(tail->at< 1>(), 5);
  EXPECT_EQ((*tail)[2], 6);

  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values[2], 3);
}

TEST(AtLeast, ReleaseAtLeastRefusesWhenSurplusIsBelowMinimalSize) {
  At_least< 2, int> values(1, 2, 3);

  const auto tail = values.release_at_least();

  EXPECT_FALSE(tail.has_value());
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values[2], 3);
}

TEST(AtLeast, ReleaseAtLeastRefusesWhenCountExceedsSurplus) {
  At_least< 2, int> values(1, 2, 3, 4);

  const auto tail = values.release_at_least(3);

  EXPECT_FALSE(tail.has_value());
  ASSERT_EQ(values.size(), 4u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values[2], 3);
  EXPECT_EQ(values[3], 4);
}

TEST(AtLeast, ReleaseAtLeastOnList) {
  At_least< 2, int, std::list< int>> values(1, 2, 3, 4);

  const auto tail = values.release_at_least();

  ASSERT_TRUE(tail.has_value());
  ASSERT_EQ(tail->size(), 2u);
  EXPECT_EQ(tail->at< 0>(), 3);
  EXPECT_EQ(tail->at< 1>(), 4);

  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
}

TEST(AtLeast, ReleaseAtLeastOnDeque) {
  At_least< 2, int, std::deque< int>> values(1, 2, 3, 4, 5, 6);

  const auto tail = values.release_at_least(3);

  ASSERT_TRUE(tail.has_value());
  ASSERT_EQ(tail->size(), 3u);
  EXPECT_EQ(tail->at< 0>(), 4);
  EXPECT_EQ(tail->at< 1>(), 5);
  EXPECT_EQ((*tail)[2], 6);

  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values[2], 3);
}

TEST(AtLeast, ReleaseSurplusFrontSideOnVector) {
  At_least< 2, int> values(1, 2, 3, 4, 5);

  const auto released = values.release_surplus(0, Side::FRONT);

  ASSERT_TRUE(released.has_value());
  ASSERT_EQ(released->size(), 3u);
  EXPECT_EQ((*released)[0], 1);
  EXPECT_EQ((*released)[1], 2);
  EXPECT_EQ((*released)[2], 3);

  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 4);
  EXPECT_EQ(values.at< 1>(), 5);
}

TEST(AtLeast, ReleaseSurplusFrontSideOnList) {
  At_least< 2, int, std::list< int>> values(1, 2, 3, 4);

  const auto released = values.release_surplus(1, Side::FRONT);

  ASSERT_TRUE(released.has_value());
  ASSERT_EQ(released->size(), 1u);
  EXPECT_EQ(released->front(), 1);

  ASSERT_EQ(values.size(), 3u);
  expect_elements(values, {2, 3, 4});
}

TEST(AtLeast, ReleaseAtLeastWithExplicitResultMinimalSizeEnoughAndInsufficient) {
  At_least< 1, int> values(1, 2, 3, 4);

  const auto enough = values.release_at_least< 3>();
  ASSERT_TRUE(enough.has_value());
  ASSERT_EQ(enough->size(), 3u);
  EXPECT_EQ(enough->at< 0>(), 2);
  EXPECT_EQ(enough->at< 1>(), 3);
  EXPECT_EQ((*enough)[2], 4);

  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values.at< 0>(), 1);
}

TEST(AtLeast, ReleaseAtLeastWithExplicitResultMinimalSizeRefusesInsufficientTail) {
  At_least< 1, int> values(1, 2);

  const auto insufficient = values.release_at_least< 3>();

  EXPECT_FALSE(insufficient.has_value());
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values[1], 2);
}

TEST(AtLeast, ReleaseAtLeastRefusesCountBelowResultMinimalSize) {
  At_least< 1, int> values(1, 2, 3, 4);

  const auto refused = values.release_at_least< 3>(2);

  EXPECT_FALSE(refused.has_value());
  ASSERT_EQ(values.size(), 4u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(values[2], 3);
  EXPECT_EQ(values[3], 4);
}

TEST(AtLeast, ReleaseAtLeastFrontSide) {
  At_least< 1, int> values(1, 2, 3, 4);

  const auto tail = values.release_at_least< 3>(0, Side::FRONT);

  ASSERT_TRUE(tail.has_value());
  ASSERT_EQ(tail->size(), 3u);
  EXPECT_EQ(tail->at< 0>(), 1);
  EXPECT_EQ(tail->at< 1>(), 2);
  EXPECT_EQ((*tail)[2], 3);

  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values.at< 0>(), 4);
}

TEST(AtLeast, ReleaseAtLeastDefaultResultMinimalSizeFromAWeakerSource) {
  At_least< 3, int> values(1, 2, 3, 4);

  const auto tail = values.release_at_least< 1>();

  ASSERT_TRUE(tail.has_value());
  ASSERT_EQ(tail->size(), 1u);
  EXPECT_EQ(tail->at< 0>(), 4);

  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values[2], 3);
}

TEST(AtLeast, ReleaseUpToBelowEqualAndAboveTheSurplus) {
  At_least< 2, int> values(1, 2, 3, 4, 5);

  const auto below = values.release_up_to(1);
  ASSERT_EQ(below.size(), 1u);
  EXPECT_EQ(below[0], 5);
  ASSERT_EQ(values.size(), 4u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values[2], 3);
  EXPECT_EQ(values[3], 4);

  const auto equal = values.release_up_to(2);
  ASSERT_EQ(equal.size(), 2u);
  EXPECT_EQ(equal[0], 3);
  EXPECT_EQ(equal[1], 4);
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);

  const auto above = values.release_up_to(5);
  EXPECT_TRUE(above.empty());
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
}

TEST(AtLeast, ReleaseUpToZeroMovesNothing) {
  At_least< 2, int> values(1, 2, 3);

  const auto released = values.release_up_to(0);

  EXPECT_TRUE(released.empty());
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values[2], 3);
}

TEST(AtLeast, ReleaseUpToFrontSide) {
  At_least< 2, int> values(1, 2, 3, 4);

  const auto released = values.release_up_to(1, Side::FRONT);

  ASSERT_EQ(released.size(), 1u);
  EXPECT_EQ(released[0], 1);
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 2);
  EXPECT_EQ(values.at< 1>(), 3);
  EXPECT_EQ(values[2], 4);
}

template< std::size_t result_minimal_size>
inline constexpr bool release_at_least_compiles =
  requires (At_least< 2, int>& instance) { instance.template release_at_least< result_minimal_size>(); };
static_assert(!release_at_least_compiles< 0>);

namespace {

// A copyable element whose move constructor throws (in principle — it never actually does here): extract()
// must fall back to copying such a tail, mirroring std::move_if_noexcept.
struct Throwing_move {
  inline static int copies = 0;
  inline static int moves = 0;

  int value;

  explicit Throwing_move(int value) : value(value) {}
  Throwing_move(const Throwing_move& other) : value(other.value) { ++copies; }
  Throwing_move(Throwing_move&& other) noexcept(false) : value(other.value) { ++moves; }

  // std::vector::erase's generic implementation move-assigns the elements after the erased range down into
  // it, even when that range is empty (erasing up to end()) and the assignment never actually runs; the type
  // still has to provide one to compile.
  Throwing_move& operator = (Throwing_move&& other) noexcept(false) {
    value = other.value;
    return *this;
  }

  static void reset() {
    copies = 0;
    moves = 0;
  }
};

} // namespace

TEST(AtLeast, ReleaseSurplusCopiesTailWhenMoveConstructorCanThrow) {
  At_least< 2, Throwing_move> values(Throwing_move(1), Throwing_move(2), Throwing_move(3));

  Throwing_move::reset();
  const auto surplus = values.release_surplus();

  ASSERT_TRUE(surplus.has_value());
  ASSERT_EQ(surplus->size(), 1u);
  EXPECT_EQ((*surplus)[0].value, 3);
  EXPECT_EQ(Throwing_move::copies, 1);
  EXPECT_EQ(Throwing_move::moves, 0);

  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>().value, 1);
  EXPECT_EQ(values.at< 1>().value, 2);
}

TEST(AtLeast, ReleaseSurplusResultFeedsIntoMake) {
  At_least< 2, int> values(1, 2, 3, 4, 5);

  auto surplus = values.release_surplus();
  ASSERT_TRUE(surplus.has_value());

  const auto rebuilt = At_least< 2, int>::make(std::move(*surplus));

  ASSERT_TRUE(rebuilt.has_value());
  ASSERT_EQ(rebuilt->size(), 3u);
  EXPECT_EQ(rebuilt->at< 0>(), 3);
  EXPECT_EQ(rebuilt->at< 1>(), 4);
  EXPECT_EQ((*rebuilt)[2], 5);
}

TEST(AtLeast, AtIndexFrontAndBack) {
  At_least< 3, int> values(10, 20, 30);

  EXPECT_EQ(values.at< 0>(), 10);
  EXPECT_EQ(values.at< 1>(), 20);
  EXPECT_EQ(values.at< 2>(), 30);
  EXPECT_EQ(values.front(), 10);
  EXPECT_EQ(values.back(), 30);

  values.at< 1>() = 99;
  EXPECT_EQ(values.at< 1>(), 99);
}

TEST(AtLeast, RuntimeAtThrowsOutOfRange) {
  const At_least< 3, int> values(1, 2, 3);

  EXPECT_EQ(values.at(0), 1);
  EXPECT_EQ(values.at(1), 2);
  EXPECT_EQ(values.at(2), 3);
  EXPECT_THROW((void) values.at(3), std::out_of_range);
}

TEST(AtLeast, NonConstRuntimeAtWritesAndThrows) {
  At_least< 3, int> values(1, 2, 3);

  values.at(1) = 42;
  EXPECT_EQ(values.at(0), 1);
  EXPECT_EQ(values.at(1), 42);
  EXPECT_EQ(values.at(2), 3);
  EXPECT_THROW((void) values.at(3), std::out_of_range);
}

TEST(AtLeast, OperatorBracketIsUnchecked) {
  At_least< 3, int> values(1, 2, 3);

  values[1] = 42;
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 42);
  EXPECT_EQ(values[2], 3);
}

TEST(AtLeast, PushBackAndEmplaceBack) {
  At_least< 2, int> values(1, 2);

  values.push_back(3);
  int four = 4;
  values.push_back(std::move(four));
  values.emplace_back(5);

  ASSERT_EQ(values.size(), 5u);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(values[2], 3);
  EXPECT_EQ(values[3], 4);
  EXPECT_EQ(values[4], 5);
}

TEST(AtLeast, PushFrontOnDeque) {
  At_least< 2, int, std::deque< int>> values(2, 3);

  values.push_front(1);
  values.emplace_front(0);

  ASSERT_EQ(values.size(), 4u);
  EXPECT_EQ(values[0], 0);
  EXPECT_EQ(values[1], 1);
  EXPECT_EQ(values[2], 2);
  EXPECT_EQ(values[3], 3);
}

TEST(AtLeast, InsertAndEmplace) {
  At_least< 2, int> values(1, 4);

  const auto inserted = values.insert(values.begin() + 1, 2);
  values.emplace(inserted + 1, 3);

  ASSERT_EQ(values.size(), 4u);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(values[2], 3);
  EXPECT_EQ(values[3], 4);
}

TEST(AtLeast, ConstReferenceOverloadsOfPushAndInsert) {
  At_least< 2, int, std::deque< int>> values(2, 3);

  const int front_value = 1;
  values.push_front(front_value);

  const int back_value = 4;
  values.push_back(back_value);

  const int inserted_value = 99;
  values.insert(values.begin() + 1, inserted_value);

  ASSERT_EQ(values.size(), 5u);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 99);
  EXPECT_EQ(values[2], 2);
  EXPECT_EQ(values[3], 3);
  EXPECT_EQ(values[4], 4);
}

TEST(AtLeast, AppendRange) {
  At_least< 2, int> values(1, 2);

  values.append_range(std::vector< int>{3, 4, 5});

  ASSERT_EQ(values.size(), 5u);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(values[2], 3);
  EXPECT_EQ(values[3], 4);
  EXPECT_EQ(values[4], 5);
}

// std::list has no random access, so operator[] and at(index) must not exist for it; front/back and the
// *_back / *_front family still do.
// Whether Container is a genuine template parameter here (rather than std::list< int> written directly)
// matters: this compiler only defers a failing constraint as SFINAE when the checked expression's type
// depends on a template parameter of the immediately enclosing entity.
template< class Container>
inline constexpr bool has_operator_bracket = requires (At_least< 1, int, Container>& instance) { instance[0]; };

TEST(AtLeast, ListHasNoOperatorBracketButSupportsPushPopAtBothEnds) {
  using List_least = At_least< 1, int, std::list< int>>;

  static_assert(!has_operator_bracket< std::list< int>>);

  List_least values(1);
  values.push_back(2);
  values.push_front(0);

  ASSERT_EQ(values.size(), 3u);
  expect_elements(values, {0, 1, 2});

  const auto popped_back = values.pop_back();
  ASSERT_TRUE(popped_back.has_value());
  EXPECT_EQ(*popped_back, 2);

  const auto popped_front = values.pop_front();
  ASSERT_TRUE(popped_front.has_value());
  EXPECT_EQ(*popped_front, 0);

  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values.front(), 1);
}

TEST(AtLeast, PopBackDownToMinimalSizeThenRefuses) {
  At_least< 2, int> values(1, 2, 3);

  const auto popped = values.pop_back();
  ASSERT_TRUE(popped.has_value());
  EXPECT_EQ(*popped, 3);
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);

  const auto refused = values.pop_back();
  EXPECT_FALSE(refused.has_value());
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
}

TEST(AtLeast, PopFrontOnDeque) {
  At_least< 2, int, std::deque< int>> values(1, 2, 3);

  const auto popped = values.pop_front();
  ASSERT_TRUE(popped.has_value());
  EXPECT_EQ(*popped, 1);
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], 2);
  EXPECT_EQ(values[1], 3);

  const auto refused = values.pop_front();
  EXPECT_FALSE(refused.has_value());
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], 2);
  EXPECT_EQ(values[1], 3);
}

TEST(AtLeast, EraseSingleAcceptedAndRefused) {
  At_least< 2, int> values(1, 2, 3);

  const auto erased = values.erase(values.begin() + 1);
  ASSERT_TRUE(erased.has_value());
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 3);

  const auto refused = values.erase(values.begin());
  EXPECT_FALSE(refused.has_value());
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 3);
}

TEST(AtLeast, EraseRangeAcceptedAndRefused) {
  At_least< 2, int> values(1, 2, 3, 4, 5);

  const auto erased = values.erase(values.begin() + 1, values.begin() + 3);
  ASSERT_TRUE(erased.has_value());
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 4);
  EXPECT_EQ(values[2], 5);

  const auto refused = values.erase(values.begin(), values.begin() + 2);
  EXPECT_FALSE(refused.has_value());
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 4);
  EXPECT_EQ(values[2], 5);
}

TEST(AtLeast, EraseSingleAndRangeOnList) {
  At_least< 2, int, std::list< int>> values(1, 2, 3, 4, 5);

  auto position = values.begin();
  ++position;
  const auto erased = values.erase(position);
  ASSERT_TRUE(erased.has_value());

  ASSERT_EQ(values.size(), 4u);
  expect_elements(values, {1, 3, 4, 5});

  auto first = values.begin();
  ++first;
  auto last = first;
  ++last;
  ++last;
  const auto erased_range = values.erase(first, last);
  ASSERT_TRUE(erased_range.has_value());

  ASSERT_EQ(values.size(), 2u);
  expect_elements(values, {1, 5});
}

// resize() returns the size the object had *before* the call, since the argument already carries the new
// size back to the caller.
TEST(AtLeast, ResizeUpDownToMinimalAndRefused) {
  At_least< 2, int> values(1, 2);

  const auto grown = values.resize(4);
  ASSERT_TRUE(grown.has_value());
  EXPECT_EQ(*grown, 2u);
  ASSERT_EQ(values.size(), 4u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values[2], 0);
  EXPECT_EQ(values[3], 0);

  const auto shrunk = values.resize(2);
  ASSERT_TRUE(shrunk.has_value());
  EXPECT_EQ(*shrunk, 4u);
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);

  const auto refused = values.resize(1);
  EXPECT_FALSE(refused.has_value());
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
}

TEST(AtLeast, ResizeOnDeque) {
  At_least< 2, int, std::deque< int>> values(1, 2);

  const auto grown = values.resize(4);
  ASSERT_TRUE(grown.has_value());
  EXPECT_EQ(*grown, 2u);
  ASSERT_EQ(values.size(), 4u);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(values[2], 0);
  EXPECT_EQ(values[3], 0);
}

TEST(AtLeast, CopyProducesAnIndependentEqualObject) {
  const At_least< 2, int> original(1, 2, 3);
  At_least< 2, int> copy(original);

  EXPECT_TRUE(copy == original);

  copy[0] = 100;
  copy[1] = 200;
  copy[2] = 300;

  EXPECT_EQ(copy[0], 100);
  EXPECT_EQ(copy[1], 200);
  EXPECT_EQ(copy[2], 300);

  EXPECT_EQ(original.at< 0>(), 1);
  EXPECT_EQ(original.at< 1>(), 2);
  EXPECT_EQ(original[2], 3);
}

TEST(AtLeast, CopyAssignmentReplacesTheTarget) {
  At_least< 2, int> target(9, 9, 9);
  const At_least< 2, int> source(1, 2, 3);

  target = source;

  ASSERT_EQ(target.size(), 3u);
  EXPECT_EQ(target.at< 0>(), 1);
  EXPECT_EQ(target.at< 1>(), 2);
  EXPECT_EQ(target[2], 3);
}

TEST(AtLeast, ContainerExposesTheUnderlyingData) {
  const At_least< 2, int> values(1, 2, 3);
  const std::vector< int>& underlying = values.container();

  ASSERT_EQ(underlying.size(), 3u);
  EXPECT_EQ(underlying[0], 1);
  EXPECT_EQ(underlying[1], 2);
  EXPECT_EQ(underlying[2], 3);
}

TEST(AtLeast, SwapExchangesContentsOfBothObjects) {
  At_least< 2, int> first(1, 2, 3);
  At_least< 2, int> second(4, 5);

  first.swap(second);

  ASSERT_EQ(first.size(), 2u);
  EXPECT_EQ(first.at< 0>(), 4);
  EXPECT_EQ(first.at< 1>(), 5);

  ASSERT_EQ(second.size(), 3u);
  EXPECT_EQ(second.at< 0>(), 1);
  EXPECT_EQ(second.at< 1>(), 2);
  EXPECT_EQ(second[2], 3);
}

// At_least has no move constructor for std::swap's own default implementation to fall back on, so this
// pins that the hidden friend is what 'using std::swap;' actually finds.
TEST(AtLeast, SwapThroughStdSwap) {
  At_least< 2, int> first(1, 2, 3);
  At_least< 2, int> second(4, 5);

  using std::swap;
  swap(first, second);

  ASSERT_EQ(first.size(), 2u);
  EXPECT_EQ(first.at< 0>(), 4);
  EXPECT_EQ(first.at< 1>(), 5);

  ASSERT_EQ(second.size(), 3u);
  EXPECT_EQ(second.at< 0>(), 1);
  EXPECT_EQ(second.at< 1>(), 2);
  EXPECT_EQ(second[2], 3);
}

TEST(AtLeast, EqualityAndOrdering) {
  const At_least< 2, int> first(1, 2, 3);
  const At_least< 2, int> second(1, 2, 3);
  const At_least< 2, int> third(1, 2, 4);

  EXPECT_TRUE(first == second);
  EXPECT_FALSE(first == third);
  EXPECT_TRUE(first < third);
  EXPECT_TRUE(third > first);
}

namespace {

// No comparison operators declared: proves == / <=> are only instantiated on actual use, not merely because
// the class is instantiated for this element type.
struct Not_comparable {
  int value;
};

// Only operator< is declared, matching what a container's own synthesized three-way order needs: At_least's
// operator<=> must still work through Synth_three_way_comparable, while operator== stays unavailable.
struct Only_less {
  int value;

  bool operator < (const Only_less& other) const {
    return value < other.value;
  }
};

} // namespace

static_assert(!std::equality_comparable< Nonempty< Not_comparable>>);
static_assert(!std::three_way_comparable< Nonempty< Not_comparable>>);
static_assert(!std::equality_comparable< Nonempty< Only_less>>);

TEST(AtLeast, ElementWithOnlyOperatorLessIsOrderableButNotEqualityComparable) {
  const Nonempty< Only_less> smaller(Only_less{1});
  const Nonempty< Only_less> larger(Only_less{2});

  EXPECT_TRUE(smaller < larger);
  EXPECT_FALSE(larger < smaller);
}

TEST(AtLeast, MoveOnlyElementSupportsConstructionPushPopAndRelease) {
  At_least< 1, std::unique_ptr< int>> values(std::make_unique< int>(1));

  values.push_back(std::make_unique< int>(2));
  values.emplace_back(std::make_unique< int>(3));

  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(*values.at< 0>(), 1);
  EXPECT_EQ(*values[1], 2);
  EXPECT_EQ(*values[2], 3);

  auto popped = values.pop_back();
  ASSERT_TRUE(popped.has_value());
  EXPECT_EQ(**popped, 3);
  ASSERT_EQ(values.size(), 2u);

  auto surplus = values.release_surplus();
  ASSERT_TRUE(surplus.has_value());
  ASSERT_EQ(surplus->size(), 1u);
  EXPECT_EQ(*(*surplus)[0], 2);
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(*values.at< 0>(), 1);
}

// ---- filled() / make_filled() ----

TEST(AtLeast, FilledBuildsMoreThanMinimalSize) {
  const auto values = At_least< 2, int>::filled< 3>(7);

  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values[0], 7);
  EXPECT_EQ(values[1], 7);
  EXPECT_EQ(values[2], 7);
}

TEST(AtLeast, FilledAtExactlyMinimalSize) {
  const auto values = At_least< 2, int>::filled< 2>(5);

  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], 5);
  EXPECT_EQ(values[1], 5);
}

TEST(AtLeast, MakeFilledRefusesBelowMinimalSizeAndAcceptsEnough) {
  const auto refused = At_least< 2, int>::make_filled(1, 7);
  EXPECT_FALSE(refused.has_value());

  const auto accepted = At_least< 2, int>::make_filled(4, 7);
  ASSERT_TRUE(accepted.has_value());
  ASSERT_EQ(accepted->size(), 4u);
  EXPECT_EQ((*accepted)[0], 7);
  EXPECT_EQ((*accepted)[1], 7);
  EXPECT_EQ((*accepted)[2], 7);
  EXPECT_EQ((*accepted)[3], 7);
}

template< std::size_t count>
inline constexpr bool filled_compiles_for_at_least_2 = requires { At_least< 2, int>::template filled< count>(0); };
static_assert(!filled_compiles_for_at_least_2< 1>);

// ---- head() / surplus() ----

TEST(AtLeast, HeadIsAFixedSpanOnContiguousStorage) {
  At_least< 2, int> values(1, 2, 3, 4);

  auto head = values.head();
  static_assert(std::same_as< decltype(head), std::span< int, 2>>);
  ASSERT_EQ(head.size(), 2u);
  EXPECT_EQ(head[0], 1);
  EXPECT_EQ(head[1], 2);

  head[0] = 100;
  EXPECT_EQ(values[0], 100);
}

TEST(AtLeast, SurplusIsADynamicSpanOnContiguousStorage) {
  At_least< 2, int> values(1, 2, 3, 4);

  auto surplus = values.surplus();
  static_assert(std::same_as< decltype(surplus), std::span< int>>);
  ASSERT_EQ(surplus.size(), 2u);
  EXPECT_EQ(surplus[0], 3);
  EXPECT_EQ(surplus[1], 4);

  surplus[0] = 300;
  EXPECT_EQ(values[2], 300);
}

TEST(AtLeast, HeadAndSurplusConstOverloadsOnContiguousStorage) {
  const At_least< 2, int> values(1, 2, 3, 4);

  auto head = values.head();
  static_assert(std::same_as< decltype(head), std::span< const int, 2>>);
  EXPECT_EQ(head[0], 1);
  EXPECT_EQ(head[1], 2);

  auto surplus = values.surplus();
  static_assert(std::same_as< decltype(surplus), std::span< const int>>);
  ASSERT_EQ(surplus.size(), 2u);
  EXPECT_EQ(surplus[0], 3);
  EXPECT_EQ(surplus[1], 4);
}

TEST(AtLeast, HeadAndSurplusAreSubrangesOnList) {
  At_least< 2, int, std::list< int>> values(1, 2, 3, 4);

  const auto head = values.head();
  auto head_iterator = head.begin();
  EXPECT_EQ(*head_iterator, 1);
  ++head_iterator;
  EXPECT_EQ(*head_iterator, 2);
  ++head_iterator;
  EXPECT_EQ(head_iterator, head.end());

  const auto surplus = values.surplus();
  auto surplus_iterator = surplus.begin();
  EXPECT_EQ(*surplus_iterator, 3);
  ++surplus_iterator;
  EXPECT_EQ(*surplus_iterator, 4);
  ++surplus_iterator;
  EXPECT_EQ(surplus_iterator, surplus.end());
}

// ---- try_at() ----

TEST(AtLeast, TryAtInRangeAndOutOfRange) {
  At_least< 2, int> values(1, 2, 3);

  auto in_range = values.try_at(1);
  ASSERT_TRUE(in_range);
  EXPECT_EQ(*in_range, 2);
  *in_range = 42;
  EXPECT_EQ(values[1], 42);

  const auto out_of_range = values.try_at(3);
  EXPECT_FALSE(out_of_range);
}

TEST(AtLeast, TryAtConstOverload) {
  const At_least< 2, int> values(1, 2, 3);

  const auto in_range = values.try_at(2);
  ASSERT_TRUE(in_range);
  EXPECT_EQ(*in_range, 3);

  const auto out_of_range = values.try_at(3);
  EXPECT_FALSE(out_of_range);
}

// ---- transform() / zip() ----

TEST(AtLeast, TransformPreservesSizeAndConvertsElements) {
  const At_least< 3, int> values(1, 2, 3, 4);

  const auto transformed = values.transform([](const int& value) { return std::to_string(value); });

  using Transformed = At_least< 3, std::string, std::vector< std::string>>;
  static_assert(std::same_as< decltype(transformed), const Transformed>);

  ASSERT_EQ(transformed.size(), 4u);
  EXPECT_EQ(transformed[0], "1");
  EXPECT_EQ(transformed[1], "2");
  EXPECT_EQ(transformed[2], "3");
  EXPECT_EQ(transformed[3], "4");
}

TEST(AtLeast, TransformIntoAList) {
  const At_least< 2, int> values(1, 2, 3);

  const auto transformed = values.transform< std::list>([](const int& value) { return value * 10; });

  using Transformed = At_least< 2, int, std::list< int>>;
  static_assert(std::same_as< decltype(transformed), const Transformed>);

  ASSERT_EQ(transformed.size(), 3u);
  expect_elements(transformed, {10, 20, 30});
}

TEST(AtLeast, ZipPairsUpToTheShorterOperand) {
  const At_least< 3, int> numbers(1, 2, 3, 4);
  const At_least< 2, std::string> words("a", "b");

  const auto zipped = numbers.zip(words);

  using Zipped = At_least< 2, std::tuple< int, std::string>, std::vector< std::tuple< int, std::string>>>;
  static_assert(std::same_as< decltype(zipped), const Zipped>);

  ASSERT_EQ(zipped.size(), 2u);
  EXPECT_EQ(zipped[0], std::make_tuple(1, std::string("a")));
  EXPECT_EQ(zipped[1], std::make_tuple(2, std::string("b")));
}

// ---- append_range() ----

template< class Range>
inline constexpr bool append_range_compiles =
  requires (Nonempty< std::unique_ptr< int>>& instance, Range&& range) {
    instance.append_range(std::forward< Range>(range));
  };
static_assert(!append_range_compiles< std::vector< std::unique_ptr< int>>>);

TEST(AtLeast, AppendRangeMovesElementsThroughAsRvalue) {
  Nonempty< std::unique_ptr< int>> values(std::make_unique< int>(1));

  std::vector< std::unique_ptr< int>> range;
  range.push_back(std::make_unique< int>(2));
  range.push_back(std::make_unique< int>(3));
  const int* const second_address = range[0].get();
  const int* const third_address = range[1].get();

  values.append_range(std::move(range) | std::views::as_rvalue);

  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(*values.at< 0>(), 1);
  EXPECT_EQ(values[1].get(), second_address);
  EXPECT_EQ(values[2].get(), third_address);
  EXPECT_EQ(*values[1], 2);
  EXPECT_EQ(*values[2], 3);
}

// ---- insert_range() / prepend_range() ----

TEST(AtLeast, InsertRangeInTheMiddleOfAVector) {
  At_least< 2, int> values(1, 5);

  const auto inserted = values.insert_range(values.begin() + 1, std::vector< int>{2, 3, 4});

  ASSERT_EQ(*inserted, 2);
  ASSERT_EQ(values.size(), 5u);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(values[2], 3);
  EXPECT_EQ(values[3], 4);
  EXPECT_EQ(values[4], 5);
}

TEST(AtLeast, InsertRangeWithAnEmptyRangeReturnsThePosition) {
  At_least< 2, int> values(1, 2);

  const auto position = values.begin() + 1;
  const auto inserted = values.insert_range(position, std::vector< int>{});

  EXPECT_EQ(inserted, position);
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
}

TEST(AtLeast, InsertRangeOnList) {
  At_least< 2, int, std::list< int>> values(1, 5);

  auto position = values.begin();
  ++position;
  values.insert_range(position, std::vector< int>{2, 3, 4});

  ASSERT_EQ(values.size(), 5u);
  expect_elements(values, {1, 2, 3, 4, 5});
}

TEST(AtLeast, PrependRangeOnDeque) {
  At_least< 2, int, std::deque< int>> values(4, 5);

  values.prepend_range(std::vector< int>{1, 2, 3});

  ASSERT_EQ(values.size(), 5u);
  EXPECT_EQ(values[0], 1);
  EXPECT_EQ(values[1], 2);
  EXPECT_EQ(values[2], 3);
  EXPECT_EQ(values[3], 4);
  EXPECT_EQ(values[4], 5);
}

TEST(AtLeast, PrependRangeOnList) {
  At_least< 2, int, std::list< int>> values(4, 5);

  values.prepend_range(std::vector< int>{1, 2, 3});

  ASSERT_EQ(values.size(), 5u);
  expect_elements(values, {1, 2, 3, 4, 5});
}

template< class Container>
inline constexpr bool prepend_range_compiles =
  requires (At_least< 1, int, Container>& instance, std::vector< int>&& range) {
    instance.prepend_range(std::move(range));
  };
static_assert(!prepend_range_compiles< std::vector< int>>);

// ---- erase_if() ----

TEST(AtLeast, EraseIfAcceptedRemovesMatchingElements) {
  At_least< 2, int> values(1, 2, 3, 4, 5);

  const auto removed = values.erase_if([](const int& value) { return value % 2 == 0; });

  ASSERT_TRUE(removed.has_value());
  EXPECT_EQ(*removed, 2u);
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 3);
  EXPECT_EQ(values[2], 5);
}

TEST(AtLeast, EraseIfRefusedLeavesTheObjectUnchanged) {
  At_least< 2, int> values(1, 2, 3);

  const auto refused = values.erase_if([](const int& value) { return value != 1; });

  EXPECT_FALSE(refused.has_value());
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values[2], 3);
}

TEST(AtLeast, EraseIfWithZeroMatches) {
  At_least< 2, int> values(1, 2, 3);

  const auto removed = values.erase_if([](const int& value) { return value > 100; });

  ASSERT_TRUE(removed.has_value());
  EXPECT_EQ(*removed, 0u);
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values[2], 3);
}

TEST(AtLeast, EraseIfOnList) {
  At_least< 2, int, std::list< int>> values(1, 2, 3, 4, 5);

  const auto removed = values.erase_if([](const int& value) { return value % 2 == 0; });

  ASSERT_TRUE(removed.has_value());
  EXPECT_EQ(*removed, 2u);
  ASSERT_EQ(values.size(), 3u);
  expect_elements(values, {1, 3, 5});
}

// Correct (single-pass) behaviour evaluates the predicate exactly once per element (calls 1 through 5),
// seeing only call 5 as a match. A buggy implementation that runs a separate counting pass followed by a
// separate erasing pass would evaluate the same stateful predicate 10 times total, so the erasing pass would
// additionally see calls 7, 9 and 10 and remove three elements instead of the one actually counted — dropping
// the size to 2, below MINIMAL_SIZE.
TEST(AtLeast, EraseIfEvaluatesEachElementExactlyOnce) {
  At_least< 3, int> values(1, 2, 3, 4, 5);

  int calls = 0;
  const auto removed = values.erase_if([&calls](const int&) {
    ++calls;
    return calls == 5 || calls == 7 || calls == 9 || calls == 10;
  });

  ASSERT_TRUE(removed.has_value());
  EXPECT_EQ(*removed, 1u);
  EXPECT_EQ(calls, 5);
  ASSERT_EQ(values.size(), 4u);
  expect_elements(values, {1, 2, 3, 4});
}

// ---- resize(count, value) ----

TEST(AtLeast, ResizeWithValueGrowsUsingTheFillValue) {
  At_least< 2, int> values(1, 2);

  const auto grown = values.resize(4, 9);
  ASSERT_TRUE(grown.has_value());
  EXPECT_EQ(*grown, 2u);
  ASSERT_EQ(values.size(), 4u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values[2], 9);
  EXPECT_EQ(values[3], 9);
}

TEST(AtLeast, ResizeWithValueShrinksAndRefuses) {
  At_least< 2, int> values(1, 2, 3, 4);

  const auto shrunk = values.resize(2, 9);
  ASSERT_TRUE(shrunk.has_value());
  EXPECT_EQ(*shrunk, 4u);
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);

  const auto refused = values.resize(1, 9);
  EXPECT_FALSE(refused.has_value());
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
}

// ---- swap(Container&) ----

TEST(AtLeast, SwapWithContainerAcceptedWhenLargeEnough) {
  At_least< 2, int> values(1, 2, 3);
  std::vector< int> other{10, 20};

  const bool swapped = values.swap(other);

  EXPECT_TRUE(swapped);
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values.at< 0>(), 10);
  EXPECT_EQ(values.at< 1>(), 20);
  ASSERT_EQ(other.size(), 3u);
  EXPECT_EQ(other[0], 1);
  EXPECT_EQ(other[1], 2);
  EXPECT_EQ(other[2], 3);
}

TEST(AtLeast, SwapWithContainerRefusedWhenTooSmall) {
  At_least< 2, int> values(1, 2, 3);
  std::vector< int> other{10};

  const bool swapped = values.swap(other);

  EXPECT_FALSE(swapped);
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values.at< 0>(), 1);
  EXPECT_EQ(values.at< 1>(), 2);
  EXPECT_EQ(values[2], 3);
  ASSERT_EQ(other.size(), 1u);
  EXPECT_EQ(other[0], 10);
}

// ---- Is_at_least / At_least_like / At_least_of ----

static_assert(Is_at_least< At_least< 3, int>>::value);
static_assert(!Is_at_least< int>::value);
static_assert(At_least_like< At_least< 3, int>>);
static_assert(At_least_like< const At_least< 3, int>&>);
static_assert(!At_least_like< int>);
static_assert((At_least_of< At_least< 3, int>, 2, int>));
static_assert(!(At_least_of< At_least< 1, int>, 2, int>));
static_assert(!(At_least_of< At_least< 2, long>, 2, int>));
static_assert((At_least_of< At_least< 3, int, std::list< int>>, 2, int>));
static_assert((At_least_of< const At_least< 3, int>&, 2, int>));
static_assert((At_least_of< At_least< 3, int>&, 2, int>));

template< At_least_of< 2, int> R>
int second(const R& values) {
  return values[1];
}

TEST(AtLeast, AtLeastOfConstrainsAFunctionTemplate) {
  const At_least< 3, int> values(10, 20, 30);

  EXPECT_EQ(second(values), 20);
}

// ---- converting constructor ----

TEST(AtLeast, ConvertingConstructorCopiesAllElementsFromAStrongerGuarantee) {
  const At_least< 3, int> stronger(1, 2, 3);
  const Nonempty< int> weaker(stronger);

  ASSERT_EQ(weaker.size(), 3u);
  EXPECT_EQ(weaker.at< 0>(), 1);
  EXPECT_EQ(weaker[1], 2);
  EXPECT_EQ(weaker[2], 3);
}

// A greedy element type (std::any) makes the variadic constructor call-compatible with the converting
// constructor whenever 'other' is the sole argument; the At_least_like guard on the variadic constructor
// keeps this reaching the converting constructor instead of wrapping 'stronger' as one element.
TEST(AtLeast, ConvertingConstructorIsChosenOverTheVariadicConstructorForAGreedyElementType) {
  At_least< 3, std::any> stronger(std::any(1), std::any(2), std::any(3));
  const Nonempty< std::any> weaker(stronger);

  ASSERT_EQ(weaker.size(), 3u);
  EXPECT_EQ(std::any_cast< int>(weaker.at< 0>()), 1);
  EXPECT_EQ(std::any_cast< int>(weaker[1]), 2);
  EXPECT_EQ(std::any_cast< int>(weaker[2]), 3);
}

static_assert(!std::is_constructible_v< At_least< 3, int>, const Nonempty< int>&>);

// ---- constexpr ----

namespace {

constexpr bool variadic_construction_and_at_and_size_are_constexpr() {
  const At_least< 3, int> values(1, 2, 3);
  return values.at< 0>() == 1 && values.at< 1>() == 2 && values.at< 2>() == 3 && values.size() == 3u;
}

constexpr bool push_and_pop_back_are_constexpr() {
  At_least< 2, int> values(1, 2);
  values.push_back(3);
  const auto popped = values.pop_back();
  return popped.has_value() && *popped == 3 && values.size() == 2u;
}

constexpr bool release_surplus_is_constexpr() {
  At_least< 2, int> values(1, 2, 3, 4);
  const auto surplus = values.release_surplus();
  return surplus.has_value() && surplus->size() == 2u && values.size() == 2u;
}

constexpr bool make_failure_is_constexpr() {
  const auto result = At_least< 3, int>::make(std::vector< int>{1, 2});
  return !result.has_value() && result.error().size() == 2u;
}

constexpr bool transform_is_constexpr() {
  const At_least< 2, int> values(1, 2, 3);
  const auto transformed = values.transform([](const int& value) { return value * 2; });
  return transformed.size() == 3u && transformed[0] == 2 && transformed[1] == 4 && transformed[2] == 6;
}

} // namespace

static_assert(variadic_construction_and_at_and_size_are_constexpr());
static_assert(push_and_pop_back_are_constexpr());
static_assert(release_surplus_is_constexpr());
static_assert(make_failure_is_constexpr());
static_assert(transform_is_constexpr());
