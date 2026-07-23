#include "core/utils/enum_array.hpp"

#include <gtest/gtest.h>

#include <any>
#include <stdexcept>
#include <string>
#include <utility>

using namespace core::utils;
using namespace core::utils::string;

// ---- dense enum with names declared in enum order ----
enum class Color { RED, GREEN, BLUE };

namespace core::utils::string {
template<>
class Enum_with_names< Color> : public Enum_with_names_base<
  Named_enum_value< Color::RED,   Constexpr_string<"RED">>,
  Named_enum_value< Color::GREEN, Constexpr_string<"GREEN">>,
  Named_enum_value< Color::BLUE,  Constexpr_string<"BLUE">>
> {};
} // namespace core::utils::string

// ---- dense enum with names declared out of enum order ----
enum class Direction { NORTH, SOUTH, EAST, WEST };

namespace core::utils::string {
template<>
class Enum_with_names< Direction> : public Enum_with_names_base<
  Named_enum_value< Direction::SOUTH, Constexpr_string<"SOUTH">>,
  Named_enum_value< Direction::NORTH, Constexpr_string<"NORTH">>,
  Named_enum_value< Direction::WEST,  Constexpr_string<"WEST">>,
  Named_enum_value< Direction::EAST,  Constexpr_string<"EAST">>
> {};
} // namespace core::utils::string

// ---- enum carrying an enumerator that was never declared in the specialization ----
// The specialization stays dense ({0, 1}), so this compiles: nothing can prove UNDECLARED is missing.
enum class Shade { LIGHT, DARK, UNDECLARED };

namespace core::utils::string {
template<>
class Enum_with_names< Shade> : public Enum_with_names_base<
  Named_enum_value< Shade::LIGHT, Constexpr_string<"LIGHT">>,
  Named_enum_value< Shade::DARK,  Constexpr_string<"DARK">>
> {};
} // namespace core::utils::string

// ---- single-value enum: guards the copy constructor against the variadic one ----
enum class Single { ONLY };

namespace core::utils::string {
template<>
class Enum_with_names< Single> : public Enum_with_names_base<
  Named_enum_value< Single::ONLY, Constexpr_string<"ONLY">>
> {};
} // namespace core::utils::string

// ---- constexpr usage: aggregate initialization + operator[] read ----
constexpr Enum_array< Color, int> COLOR_WEIGHTS(10, 20, 30);
static_assert(COLOR_WEIGHTS[Color::RED] == 10);
static_assert(COLOR_WEIGHTS[Color::GREEN] == 20);
static_assert(COLOR_WEIGHTS[Color::BLUE] == 30);
static_assert(Enum_array< Color, int>::SIZE == 3);

// ---- compile-time checked access ----
static_assert(COLOR_WEIGHTS.at< Color::RED>() == 10);
static_assert(COLOR_WEIGHTS.at< Color::BLUE>() == 30);
static_assert(noexcept(COLOR_WEIGHTS.at< Color::RED>()));

// operator[] is unchecked like std::array's, at() validates the runtime key and may throw.
static_assert(noexcept(COLOR_WEIGHTS[Color::RED]));
static_assert(!noexcept(COLOR_WEIGHTS.at(Color::RED)));
static_assert(COLOR_WEIGHTS.at(Color::GREEN) == 20);

TEST(EnumArray, RuntimeReadWrite) {
  Enum_array< Color, std::string> names;
  names[Color::RED] = "red";
  names[Color::GREEN] = "green";
  names[Color::BLUE] = "blue";

  EXPECT_EQ(names[Color::RED], "red");
  EXPECT_EQ(names[Color::GREEN], "green");
  EXPECT_EQ(names[Color::BLUE], "blue");
}

TEST(EnumArray, Size) {
  EXPECT_EQ((Enum_array< Color, int>::SIZE), 3u);
  EXPECT_EQ((Enum_array< Direction, int>::SIZE), 4u);
}

TEST(EnumArray, RangeForIteration) {
  const Enum_array< Color, int> weights(1, 2, 3);

  int sum = 0;
  for (const int weight : weights) {
    sum += weight;
  }

  EXPECT_EQ(sum, 6);
}

TEST(EnumArray, UnorderedDeclarationStillIndexesByUnderlyingValue) {
  Enum_array< Direction, int> steps;
  steps[Direction::NORTH] = 1;
  steps[Direction::SOUTH] = 2;
  steps[Direction::EAST] = 3;
  steps[Direction::WEST] = 4;

  EXPECT_EQ(steps[Direction::NORTH], 1);
  EXPECT_EQ(steps[Direction::SOUTH], 2);
  EXPECT_EQ(steps[Direction::EAST], 3);
  EXPECT_EQ(steps[Direction::WEST], 4);
}

TEST(EnumArray, CompileTimeCheckedAccess) {
  Enum_array< Color, std::string> names;
  names.at< Color::GREEN>() = "green";

  EXPECT_EQ(names.at< Color::GREEN>(), "green");
  EXPECT_EQ(names[Color::GREEN], "green");

  const Enum_array< Color, int> weights(1, 2, 3);
  EXPECT_EQ(weights.at< Color::BLUE>(), 3);
}

TEST(EnumArray, CheckedAccessWithRuntimeKey) {
  Enum_array< Color, int> weights;
  weights.at(Color::RED) = 10;

  EXPECT_EQ(weights.at(Color::RED), 10);
  EXPECT_EQ(std::as_const(weights).at(Color::RED), 10);
}

// Undeclared enumerators keep the specialization dense ({0, 1}), so nothing can reject Shade::UNDECLARED
// at compile time. Only at() catches it; operator[] would index out of bounds.
TEST(EnumArray, UndeclaredEnumeratorThrowsFromCheckedAccess) {
  Enum_array< Shade, int> shades;
  shades[Shade::LIGHT] = 1;

  EXPECT_EQ(shades[Shade::LIGHT], 1);
  EXPECT_THROW(shades.at(Shade::UNDECLARED), std::out_of_range);
  EXPECT_THROW(std::as_const(shades).at(Shade::UNDECLARED), std::out_of_range);
}

TEST(EnumArray, ValueCastFromArbitraryIntegerThrowsFromCheckedAccess) {
  Enum_array< Color, int> weights;

  EXPECT_THROW(weights.at(static_cast< Color>(99)), std::out_of_range);
  EXPECT_THROW(weights.at(static_cast< Color>(-1)), std::out_of_range);
}

// A greedy converting T (std::any) plus SIZE == 1 makes the variadic constructor
// call-compatible with copying: without a guard it wraps the source array instead of copying it.
TEST(EnumArray, CopyConstructionIsNotHijackedByVariadicConstructor) {
  Enum_array< Single, std::any> source;
  source[Single::ONLY] = 42;

  Enum_array< Single, std::any> copy(source);

  ASSERT_TRUE(copy[Single::ONLY].has_value());
  EXPECT_EQ(std::any_cast< int>(copy[Single::ONLY]), 42);
}

TEST(EnumArray, EnumWithNamesStillWorksForSameEnum) {
  EXPECT_EQ(Enum_with_names< Color>::get_name_by_value(Color::GREEN), "GREEN");
  EXPECT_EQ(Enum_with_names< Direction>::get_name_by_value(Direction::WEST), "WEST");
}
