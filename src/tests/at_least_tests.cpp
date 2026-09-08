#include "core/utils/at_least.hpp"

#include <gtest/gtest.h>

#include <any>
#include <concepts>
#include <deque>
#include <list>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using namespace core::utils;

// ---- compile-time refusals ----
static_assert(!std::is_default_constructible_v< At_least< 3, int>>);
static_assert(!std::is_move_constructible_v< At_least< 3, int>>);
static_assert(!std::is_move_assignable_v< At_least< 3, int>>);
static_assert(std::is_copy_constructible_v< At_least< 3, int>>);
static_assert(!std::is_constructible_v< At_least< 2, int>, int>);

// Single-argument construction is explicit; a bare int must not implicitly convert.
static_assert(!std::is_convertible_v< int, Nonempty< int>>);
static_assert(std::is_constructible_v< Nonempty< int>, int>);

// Narrowing is rejected the same way Enum_array rejects it: T{argument} is required in addition to T(argument).
static_assert(!std::is_constructible_v< Nonempty< int>, double>);

// A Container is never silently wrapped as one element through the variadic constructor: make(Container) is
// the only path in for a whole container.
static_assert(!std::is_constructible_v< Nonempty< std::any>, std::vector< std::any>>);
static_assert(std::is_constructible_v< Nonempty< std::any>, std::any>);

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

  EXPECT_FALSE(result.has_value());
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

  EXPECT_FALSE(result.has_value());
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

  EXPECT_FALSE(result.has_value());
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

namespace {

// A copyable element whose move constructor throws (in principle — it never actually does here): extract_tail
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
  EXPECT_GT(Throwing_move::copies, 0);
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
  EXPECT_THROW(values.at(3), std::out_of_range);
}

TEST(AtLeast, NonConstRuntimeAtWritesAndThrows) {
  At_least< 3, int> values(1, 2, 3);

  values.at(1) = 42;
  EXPECT_EQ(values.at(0), 1);
  EXPECT_EQ(values.at(1), 42);
  EXPECT_EQ(values.at(2), 3);
  EXPECT_THROW(values.at(3), std::out_of_range);
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
  auto iterator = values.begin();
  EXPECT_EQ(*iterator, 0);
  ++iterator;
  EXPECT_EQ(*iterator, 1);
  ++iterator;
  EXPECT_EQ(*iterator, 2);

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
  auto iterator = values.begin();
  EXPECT_EQ(*iterator, 1);
  ++iterator;
  EXPECT_EQ(*iterator, 3);
  ++iterator;
  EXPECT_EQ(*iterator, 4);
  ++iterator;
  EXPECT_EQ(*iterator, 5);

  auto first = values.begin();
  ++first;
  auto last = first;
  ++last;
  ++last;
  const auto erased_range = values.erase(first, last);
  ASSERT_TRUE(erased_range.has_value());

  ASSERT_EQ(values.size(), 2u);
  iterator = values.begin();
  EXPECT_EQ(*iterator, 1);
  ++iterator;
  EXPECT_EQ(*iterator, 5);
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

TEST(AtLeast, ComparisonOperatorsAreNotInstantiatedForANonComparableElementType) {
  const Nonempty< Not_comparable> values(Not_comparable{1});

  EXPECT_EQ(values.front().value, 1);
}

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
