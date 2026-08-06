#include "core/utils/general.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using namespace core::utils;

namespace {

// A type with a const field: copy/move constructible, but neither copy- nor move-assignable —
// exactly the shape std::vector rejects without Swappable.
struct Some {
  const int value;
  explicit Some(int value) : value(value) {}
};

static_assert(std::is_copy_constructible_v< Some>);
static_assert(std::is_nothrow_move_constructible_v< Some>);
static_assert(!std::is_copy_assignable_v< Some>);
static_assert(!std::is_move_assignable_v< Some>);

// A const field on the counted type too, so the counting tests exercise the exact layout that makes
// the reused-storage access launder-sensitive, not a trivially-relocatable one.
struct Counted {
  inline static int constructions = 0;
  inline static int destructions = 0;

  const int value;

  explicit Counted(int value) : value(value) { ++constructions; }
  Counted(const Counted& other) : value(other.value) { ++constructions; }

  Counted(Counted&& other) noexcept : value(other.value) { ++constructions; }

  ~Counted() { ++destructions; }
};

// Copy constructor can be armed to throw; the move constructor never does, so the type still satisfies
// Swappable's nothrow-move requirement. Used to check the strong guarantee of copy-assignment.
struct Throwing {
  inline static bool throw_on_copy = false;

  int value;

  explicit Throwing(int value) : value(value) {}

  Throwing(const Throwing& other) : value(other.value) {
    if (throw_on_copy) {
      throw std::runtime_error("Throwing: copy");
    }
  }

  Throwing(Throwing&&) noexcept = default;
};

} // namespace

// ---- *: const-ness of the exposed value follows T, both through a mutable and a const wrapper ----
static_assert(std::is_same_v< decltype(*std::declval< Swappable< const Some>&>()), const Some&>);
static_assert(std::is_same_v< decltype(*std::declval< const Swappable< const Some>&>()), const Some&>);

// ---- Swappable stays move-only when Value is move-only ----
static_assert(!std::is_copy_constructible_v< Swappable< std::unique_ptr< int>>>);

// ---- constexpr usage: build, move-assign, read ----
constexpr int make_constexpr_swappable_value() {
  Swappable< const int> a(1);
  Swappable< const int> b(2);
  a = std::move(b);
  return *a;
}

static_assert(make_constexpr_swappable_value() == 2);

TEST(Swappable, VectorOfConstFieldTypeSortsAndErases) {
  std::vector< Swappable< const Some>> values;
  values.emplace_back(3);
  values.emplace_back(1);
  values.emplace_back(2);

  std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
    return (*left).value < (*right).value;
  });

  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ((*values[0]).value, 1);
  EXPECT_EQ((*values[1]).value, 2);
  EXPECT_EQ((*values[2]).value, 3);

  values.erase(values.begin() + 1);

  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ((*values[0]).value, 1);
  EXPECT_EQ((*values[1]).value, 3);
}

TEST(Swappable, NonConstValueIsMutableThroughDereference) {
  Swappable< int> value(5);
  *value = 7;

  EXPECT_EQ(*value, 7);
}

TEST(Swappable, AssignmentFromValueReplacesTheWrappedObject) {
  Swappable< const Some> wrapper(1);

  const Some replacement(2);
  wrapper = replacement;                 // copy from an lvalue value
  EXPECT_EQ((*wrapper).value, 2);

  wrapper = Some(3);                      // move from an rvalue value
  EXPECT_EQ((*wrapper).value, 3);
}

TEST(Swappable, AssignmentFromValueWorksForMoveOnlyValue) {
  Swappable< std::unique_ptr< int>> wrapper(std::make_unique< int>(1));

  wrapper = std::make_unique< int>(2);
  EXPECT_EQ(**wrapper, 2);
}

TEST(Swappable, AssignmentFromValueLeavesTargetIntactWhenCopyThrows) {
  Swappable< const Throwing> wrapper(1);

  const Throwing replacement(2);
  Throwing::throw_on_copy = true;
  EXPECT_THROW(wrapper = replacement, std::runtime_error);
  Throwing::throw_on_copy = false;

  EXPECT_EQ((*wrapper).value, 1);
}

TEST(Swappable, MoveOnlyValueInVector) {
  std::vector< Swappable< std::unique_ptr< int>>> values;
  values.emplace_back(std::make_unique< int>(1));
  values.emplace_back(std::make_unique< int>(2));

  values[0] = std::move(values[1]);

  EXPECT_EQ(**values[0], 2);
}

TEST(Swappable, CopyAssignmentReplacesValueAndLeavesSourceIntact) {
  Swappable< const Some> first(1);
  const Swappable< const Some> second(2);

  first = second;

  EXPECT_EQ((*first).value, 2);
  EXPECT_EQ((*second).value, 2);
}

TEST(Swappable, SelfAssignmentIsANoOp) {
  Swappable< const Some> value(5);
  const Swappable< const Some>* const self = &value;

  value = *self;

  EXPECT_EQ((*value).value, 5);

  value = std::move(*const_cast< Swappable< const Some>*>(self));

  EXPECT_EQ((*value).value, 5);
}

TEST(Swappable, CopyAssignmentLeavesTargetIntactWhenCopyThrows) {
  Swappable< const Throwing> first(1);
  const Swappable< const Throwing> second(2);

  Throwing::throw_on_copy = true;
  EXPECT_THROW(first = second, std::runtime_error);
  Throwing::throw_on_copy = false;

  EXPECT_EQ((*first).value, 1);
}

TEST(Swappable, MoveAssignmentDestroysAndReconstructsExactlyOnce) {
  Counted::constructions = 0;
  Counted::destructions = 0;

  Swappable< const Counted> first(1);
  Swappable< const Counted> second(2);
  const int constructions_before = Counted::constructions;
  const int destructions_before = Counted::destructions;

  first = std::move(second);

  EXPECT_EQ(Counted::constructions - constructions_before, 1);
  EXPECT_EQ(Counted::destructions - destructions_before, 1);
  EXPECT_EQ((*first).value, 2);
}

TEST(Swappable, CopyAssignmentConstructsTemporaryThenReconstructs) {
  Counted::constructions = 0;
  Counted::destructions = 0;

  Swappable< const Counted> first(1);
  const Swappable< const Counted> second(2);
  const int constructions_before = Counted::constructions;
  const int destructions_before = Counted::destructions;

  first = second;

  // temp copy (+1c), destroy old target (+1d), move-construct into target (+1c), destroy temp (+1d).
  EXPECT_EQ(Counted::constructions - constructions_before, 2);
  EXPECT_EQ(Counted::destructions - destructions_before, 2);
  EXPECT_EQ((*first).value, 2);
}
