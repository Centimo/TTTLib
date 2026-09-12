#include "core/utils/ranges.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <concepts>
#include <ranges>
#include <span>
#include <string>
#include <sstream>
#include <tuple>
#include <utility>
#include <vector>

using namespace core::utils;
using std::views::transform;

namespace {

constexpr std::array< size_t, 3> POSITION{2, 3, 4};
constexpr std::array< size_t, 3> STRIDES{100, 10, 1};

enum class Scoped : int { One = 1 };

// Stands in for the elementwise adaptors where the test is about zip_with itself, not the arithmetic.
constexpr auto product = [](const auto&... components) { return (... * components); };

} // namespace

TEST(Ranges, LinearIndexIsAChainOfIndependentSteps) {
  EXPECT_EQ(POSITION | multiply(STRIDES) | sum, 234u);
}

TEST(Ranges, ChainIsUsableAtCompileTime) {
  static_assert((POSITION | multiply(STRIDES) | sum) == 234u);
  static_assert((std::array{1, 2} | multiply(std::array{3, 4}) | sum) == 11);
}

// The piped range must become component 0 of every tuple. Both multiply and sum are commutative, so a
// swapped order would go unnoticed by the other tests — this one looks at the tuples themselves and
// reduces with a non-commutative operation.
TEST(Ranges, ZipWithPutsThePipedRangeFirst) {
  const std::array< int, 2> left{1, 2};
  const std::array< int, 2> right{10, 20};

  const auto& [first, second] = *std::ranges::begin(left | zip_with(right));
  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 10);

  const auto difference = [](const auto& components) {
    const auto& [minuend, subtrahend] = components;
    return subtrahend - minuend;
  };

  EXPECT_EQ(left | zip_with(right) | transform(difference) | sum, 27);
}

TEST(Ranges, ZipWithAcceptsSeveralRanges) {
  const std::array< size_t, 3> extra{1, 2, 3};
  EXPECT_EQ(POSITION | multiply(STRIDES, extra) | sum, 272u);
}

TEST(Ranges, ZipWithAcceptsABorrowedRange) {
  const std::span< const size_t> strides(STRIDES);
  EXPECT_EQ(POSITION | zip_with(strides) | unpack(product) | sum, 234u);
}

// The temporary vector dies at the end of the first statement. Reading the zipped view afterwards is only
// valid because zip_with moved the vector into an owning_view — a stored ref_view would dangle here.
TEST(Ranges, ZipWithOwnsATemporaryRangeBeyondTheFullExpression) {
  auto zipped = POSITION | zip_with(std::vector< size_t>{5, 5, 5});

  // A ref_view would leave the zipped view copyable; owning_view is move-only, which pins the ownership
  // claim at compile time instead of relying on the read below to notice the dangling reference.
  static_assert(!std::copy_constructible< decltype(zipped)>);

  EXPECT_EQ(std::move(zipped) | unpack(product) | sum, 45u);
}

TEST(Ranges, StoredClosureAppliesToDifferentLeftRanges) {
  const auto with_strides = zip_with(STRIDES);
  const std::array< size_t, 3> other{1, 1, 1};

  EXPECT_EQ(POSITION | with_strides | unpack(product) | sum, 234u);
  EXPECT_EQ(other | with_strides | unpack(product) | sum, 111u);
}

TEST(Ranges, ZipWithStopsAtTheShortestRange) {
  const std::array< size_t, 2> shorter{7, 7};
  EXPECT_EQ(POSITION | zip_with(shorter) | unpack(product) | sum, 35u);
}

TEST(Ranges, SumFoldsAScalarRange) {
  EXPECT_EQ(POSITION | sum, 9u);
  EXPECT_EQ(std::views::iota(1, 5) | sum, 10);
}

TEST(Ranges, SumOfAnEmptyRangeIsZero) {
  const std::vector< size_t> empty;
  EXPECT_EQ(empty | sum, 0u);
}

TEST(Ranges, SumAcceptsSinglePassInputRange) {
  std::istringstream stream("1 2 3 4");
  EXPECT_EQ(std::views::istream< int>(stream) | sum, 10);
}

TEST(Ranges, ElementwiseOperationsTakeSeveralRanges) {
  const std::array< int, 3> left{100, 60, 9};
  const std::array< int, 3> right{10, 6, 3};
  const std::array< int, 3> third{2, 3, 1};
  const std::array< int, 3> sevens{7, 7, 7};

  EXPECT_EQ(left | divide(right) | sum, 23);
  EXPECT_EQ(left | divide(right, third) | sum, 11);        // (100/10)/2 + (60/6)/3 + (9/3)/1
  EXPECT_EQ(left | add(right) | sum, 188);
  EXPECT_EQ(left | subtract(right, third) | sum, 144);     // (100-10-2) + (60-6-3) + (9-3-1)
  EXPECT_EQ(left | modulo(sevens) | sum, 8);            // 100%7 + 60%7 + 9%7 = 2 + 4 + 2
  EXPECT_EQ(left | modulo(sevens, third) | sum, 1);     // (100%7)%2 + (60%7)%3 + (9%7)%1 = 0 + 1 + 0
  EXPECT_EQ(left | multiply(right, third) | sum, 3107);
}

// uint32_t gets no help from integral promotion, so without widening the products wrap around — both
// outcomes are asserted so that the widening step cannot be dropped unnoticed.
TEST(Ranges, WideningIsASeparateStep) {
  const std::array< uint32_t, 2> left{100000, 100000};
  const std::array< uint32_t, 2> right{100000, 100000};
  EXPECT_EQ(left | multiply(right) | sum, 2820130816u);
  EXPECT_EQ(left | as< std::uint64_t> | multiply(right | as< std::uint64_t>) | sum, 20000000000ull);
}

// The scalar side of a chain is a range as well: repeat is infinite and zip_transform stops at the
// shortest, so the decoding needs no lambda at all.
TEST(Ranges, RepeatSuppliesTheScalarOperand) {
  const std::array< std::size_t, 3> strides{100, 10, 1};
  const std::array< std::size_t, 3> sizes{8, 8, 8};
  const std::size_t linear_index = 234;

  const auto decoded = std::views::repeat(linear_index)
                       | divide(strides)
                       | modulo(sizes)
                       | cast< std::int32_t>;
  static_assert(std::same_as< std::ranges::range_value_t< decltype(decoded)>, std::int32_t>);

  EXPECT_EQ(decoded | std::ranges::to< std::vector< std::int32_t>>(), (std::vector< std::int32_t>{2, 7, 2}));
}

TEST(Ranges, ElementwiseStopsAtTheShortestRange) {
  const std::array< size_t, 2> shorter{7, 7};
  EXPECT_EQ(POSITION | multiply(shorter) | sum, 35u);
}

TEST(Ranges, StoredElementwiseClosureAppliesToDifferentLeftRanges) {
  const auto by_strides = multiply(STRIDES);
  const std::array< std::size_t, 3> other{1, 1, 1};

  EXPECT_EQ(POSITION | by_strides | std::ranges::to< std::vector< std::size_t>>(),
            (std::vector< std::size_t>{200, 30, 4}));
  EXPECT_EQ(other | by_strides | sum, 111u);
}

// A type mismatch is a rejected pipe, not an error from inside zip_transform: the operations carry a
// trailing return type, so std::invocable can answer for them.
TEST(Ranges, ElementwiseRejectsOperationsTheTypesDoNotSupport) {
  const std::array< double, 2> fractions{1.0, 2.0};

  static_assert(!std::invocable< decltype(modulo(fractions)), std::array< double, 2>&>);
  static_assert(std::invocable< decltype(multiply(fractions)), std::array< double, 2>&>);
}

TEST(Ranges, ElementwiseOwnsATemporaryRange) {
  auto products = POSITION | multiply(std::vector< size_t>{5, 5, 5});
  static_assert(!std::copy_constructible< decltype(products)>);

  EXPECT_EQ(std::move(products) | sum, 45u);
}

// The arithmetic alone would produce 234 either way (int32_t * size_t promotes), so the element type of
// the intermediate range is what pins the conversion.
TEST(Ranges, CastCarriesTheChainAcrossTypes) {
  const std::array< std::int32_t, 3> position{2, 3, 4};
  const auto widened = position | cast< std::size_t>;

  static_assert(std::same_as< std::ranges::range_value_t< decltype(widened)>, std::size_t>);
  EXPECT_EQ(widened | multiply(STRIDES) | sum, 234u);
}

TEST(Ranges, CastNarrowsAndChangesSign) {
  const std::array< double, 2> fractions{1.7, 2.9};
  EXPECT_EQ(fractions | cast< int> | sum, 3);

  const std::array< std::int32_t, 1> negative{-1};
  EXPECT_EQ(negative | cast< std::size_t> | sum, std::numeric_limits< std::size_t>::max());
}

TEST(Ranges, AsConvertsWithoutLoss) {
  const std::array< std::uint32_t, 3> position{2, 3, 4};
  const auto widened = position | as< std::size_t>;

  static_assert(std::same_as< std::ranges::range_value_t< decltype(widened)>, std::size_t>);
  static_assert(
    std::same_as< std::ranges::range_value_t< decltype(std::views::iota(1, 4) | as< std::int64_t>)>,
    std::int64_t>);

  EXPECT_EQ(widened | multiply(STRIDES) | sum, 234u);
}

// The whole difference between the two, probed on the same ranges: as refuses every conversion that
// loses information, cast performs it. Everything else they accept alike.
TEST(Ranges, AsRefusesWhatCastPerforms) {
  static_assert(std::invocable< decltype(cast< std::size_t>), std::array< std::int32_t, 1>&>);
  static_assert(!std::invocable< decltype(as< std::size_t>), std::array< std::int32_t, 1>&>);

  static_assert(std::invocable< decltype(cast< int>), std::array< double, 1>&>);
  static_assert(!std::invocable< decltype(as< int>), std::array< double, 1>&>);

  static_assert(std::invocable< decltype(cast< int>), std::array< Scoped, 1>&>);
  static_assert(!std::invocable< decltype(as< int>), std::array< Scoped, 1>&>);

  // Value-preserving conversions go through both.
  static_assert(std::invocable< decltype(cast< std::size_t>), std::array< std::uint32_t, 1>&>);
  static_assert(std::invocable< decltype(as< std::size_t>), std::array< std::uint32_t, 1>&>);
}

TEST(Ranges, UnpackSpreadsComponentsOverArguments) {
  const std::array< size_t, 3> extra{1, 2, 3};

  const auto products = POSITION
                        | zip_with(STRIDES, extra)
                        | unpack([](const auto& first, const auto& second, const auto& third) {
                            return first * second * third;
                          });

  EXPECT_EQ(products | sum, 272u);
}

// A mismatched function is rejected by the constraint, not by an error inside std::apply — so invocable
// gives the right answer and the adaptor never forms.
TEST(Ranges, UnpackRejectsWhatItCannotCall) {
  const auto binary = [](size_t first, size_t second) { return first + second; };

  static_assert(!std::invocable< decltype(unpack(binary)), std::array< size_t, 3>&>);
  static_assert(!std::invocable< decltype(unpack(binary)), decltype(POSITION | zip_with(STRIDES, STRIDES))>);
  static_assert(std::invocable< decltype(unpack(binary)), decltype(POSITION | zip_with(STRIDES))>);
}

// views::transform accepts a mutable callable, so unpack does too — the non-const overload is what makes
// that work.
TEST(Ranges, UnpackAcceptsAMutableFunction) {
  size_t calls = 0;
  auto counting = unpack([&calls](const auto& first, const auto& second) mutable {
    ++calls;
    return first * second;
  });

  EXPECT_EQ(POSITION | zip_with(STRIDES) | counting | sum, 234u);
  EXPECT_EQ(calls, 3u);
}

TEST(Ranges, StoredUnpackClosureAppliesToDifferentRanges) {
  const auto difference = unpack([](const auto& first, const auto& second) { return second - first; });
  const std::array< size_t, 3> other{1, 1, 1};

  EXPECT_EQ(POSITION | zip_with(STRIDES) | difference | sum, 102u);
  EXPECT_EQ(other | zip_with(STRIDES) | difference | sum, 108u);
}

// The shape this file exists for: decode a linear index into coordinates without a single cast or
// structured binding inside the lambda. The static_asserts pin both conversion steps — drop either one
// and the element types stop matching, even though the coordinates would come out the same.
TEST(Ranges, DecodesALinearIndexIntoCoordinates) {
  const std::array< std::size_t, 3> strides{100, 10, 1};
  const std::array< std::int32_t, 3> sizes{8, 8, 8};
  const std::size_t linear_index = 234;

  const auto paired = strides | zip_with(sizes | cast< std::size_t>);
  static_assert(
    std::same_as< std::ranges::range_value_t< decltype(paired)>, std::tuple< std::size_t, std::size_t>>);

  const auto decoded = paired
                       | unpack([linear_index](const auto& stride, const auto& size) {
                           return linear_index / stride % size;
                         })
                       | cast< std::int32_t>;
  static_assert(std::same_as< std::ranges::range_value_t< decltype(decoded)>, std::int32_t>);

  // 234 / 100 % 8 == 2, 234 / 10 % 8 == 7, 234 / 1 % 8 == 2
  EXPECT_EQ(decoded | std::ranges::to< std::vector< std::int32_t>>(), (std::vector< std::int32_t>{2, 7, 2}));
}

TEST(Ranges, CombineZipsItsArguments) {
  EXPECT_EQ(combine(POSITION, STRIDES) | unpack(product) | sum, 234u);
}
