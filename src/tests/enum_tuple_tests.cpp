#include "core/utils/enum_array.hpp"
#include "core/utils/enum_tuple.hpp"
#include "core/utils/meta.hpp"

#include <gtest/gtest.h>

#include <any>
#include <concepts>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace core::utils;
using namespace core::utils::string;

// ---- dense enum with names declared in enum order ----
enum class Field { INDEX, LABEL, WEIGHTS };

namespace core::utils::string {
template<>
class Enum_with_names< Field> : public Enum_with_names_base<
  Named_enum_value< Field::INDEX,   Constexpr_string<"INDEX">>,
  Named_enum_value< Field::LABEL,   Constexpr_string<"LABEL">>,
  Named_enum_value< Field::WEIGHTS, Constexpr_string<"WEIGHTS">>
> {};
} // namespace core::utils::string

// ---- dense enum with names declared out of enum order ----
enum class Axis { X, Y, Z };

namespace core::utils::string {
template<>
class Enum_with_names< Axis> : public Enum_with_names_base<
  Named_enum_value< Axis::Z, Constexpr_string<"Z">>,
  Named_enum_value< Axis::X, Constexpr_string<"X">>,
  Named_enum_value< Axis::Y, Constexpr_string<"Y">>
> {};
} // namespace core::utils::string

// ---- enum carrying an enumerator that was never declared in the specialization ----
enum class Stage { START, FINISH, UNDECLARED };

namespace core::utils::string {
template<>
class Enum_with_names< Stage> : public Enum_with_names_base<
  Named_enum_value< Stage::START,  Constexpr_string<"START">>,
  Named_enum_value< Stage::FINISH, Constexpr_string<"FINISH">>
> {};
} // namespace core::utils::string

// ---- single-value enum: guards the copy constructor against the variadic one ----
enum class Only_slot { SINGLE };

namespace core::utils::string {
template<>
class Enum_with_names< Only_slot> : public Enum_with_names_base<
  Named_enum_value< Only_slot::SINGLE, Constexpr_string<"SINGLE">>
> {};
} // namespace core::utils::string

using Payload = Enum_tuple< Field, int, std::string, std::vector< double>>;
using Coordinates = Enum_tuple< Axis, int, double, char>;

// ---- constexpr construction and compile-time keyed access ----
constexpr Coordinates COORDINATES(1, 2.5, 'z');
static_assert(COORDINATES.at< Axis::X>() == 1);
static_assert(COORDINATES.at< Axis::Y>() == 2.5);
static_assert(COORDINATES.at< Axis::Z>() == 'z');
static_assert(Coordinates::SIZE == 3);
static_assert(noexcept(COORDINATES.at< Axis::X>()));

// The name resolves to the same element as the enumerator does.
static_assert(COORDINATES.at< Constexpr_string<"X">>() == 1);
static_assert(COORDINATES.at< Constexpr_string<"Y">>() == 2.5);
static_assert(COORDINATES.at< Constexpr_string<"Z">>() == 'z');

// ---- element types ----
static_assert(std::same_as< Payload::Element< Field::INDEX>, int>);
static_assert(std::same_as< Payload::Element< Field::LABEL>, std::string>);
static_assert(std::same_as< Payload::Element< Field::WEIGHTS>, std::vector< double>>);

static_assert(std::same_as< decltype(std::declval< Payload&>().at< Field::LABEL>()), std::string&>);
static_assert(std::same_as< decltype(std::declval< const Payload&>().at< Field::LABEL>()), const std::string&>);
static_assert(std::same_as< decltype(std::declval< const Payload&>().at< Constexpr_string<"LABEL">>()), const std::string&>);

// ---- what the variadic constructor accepts ----
static_assert(std::constructible_from< Coordinates, int, double, char>);
static_assert(!std::constructible_from< Coordinates, int, double>);           // too few
static_assert(!std::constructible_from< Coordinates, int, double, char, int>); // too many
static_assert(!std::constructible_from< Coordinates, double, double, char>);   // narrowing into the int element
static_assert(!std::constructible_from< Coordinates, const char*, double, char>);

// The single-element form is explicit, so the element type never converts into the container implicitly,
// and it checks narrowing exactly as the multi-element form does.
static_assert(std::constructible_from< Enum_tuple< Only_slot, int>, int>);
static_assert(!std::convertible_to< int, Enum_tuple< Only_slot, int>>);
static_assert(!std::constructible_from< Enum_tuple< Only_slot, int>, double>);

// ---- tuple protocol: positions in enum order, keys not carried over ----
static_assert(std::tuple_size_v< Payload> == 3);
static_assert(std::tuple_size_v< Coordinates> == 3);
static_assert(std::same_as< std::tuple_element_t< 0, Payload>, int>);
static_assert(std::same_as< std::tuple_element_t< 1, Payload>, std::string>);
static_assert(std::same_as< std::tuple_element_t< 2, Payload>, std::vector< double>>);
static_assert(meta::Tuple_like< Payload>);
static_assert(get< 0>(COORDINATES) == 1);
static_assert(get< 1>(COORDINATES) == 2.5);
static_assert(get< 2>(COORDINATES) == 'z');

static_assert(std::same_as< decltype(get< 1>(std::declval< Payload&>())), std::string&>);
static_assert(std::same_as< decltype(get< 1>(std::declval< const Payload&>())), const std::string&>);
static_assert(std::same_as< decltype(get< 1>(std::declval< Payload>())), std::string&&>);
static_assert(std::same_as< decltype(get< 1>(std::declval< const Payload>())), const std::string&&>);

// Copying the container is the implicit copy constructor's job at every size, not the variadic one's.
static_assert(std::copy_constructible< Coordinates>);
static_assert(std::move_constructible< Enum_tuple< Axis, std::unique_ptr< int>, std::string, char>>);
static_assert(!std::copy_constructible< Enum_tuple< Axis, std::unique_ptr< int>, std::string, char>>);

namespace {

// Counts how an element got into the tuple. Every test using it resets the counters first.
struct Tracked {
  inline static int constructions = 0;
  inline static int copies = 0;
  inline static int moves = 0;

  std::string value;

  explicit Tracked(std::string text) : value(std::move(text)) { ++constructions; }
  Tracked(const Tracked& other) : value(other.value) { ++copies; }
  Tracked(Tracked&& other) noexcept : value(std::move(other.value)) { ++moves; }

  static void reset() {
    constructions = 0;
    copies = 0;
    moves = 0;
  }
};

// Neither copyable nor movable: it can only reach the tuple if it is built where it lives, so this turns
// in-place construction from a counter reading into a compile-time requirement.
struct Immovable {
  const int value;

  explicit Immovable(int value) : value(value) {}
  Immovable(const Immovable&) = delete;
  Immovable(Immovable&&) = delete;
};

// No comparison operators at all: the container must still be usable, since its own comparisons are
// instantiated only where they are called.
struct Opaque {
  int value;
};

// An overload set rather than a generic lambda: it proves visit() reaches the element of the key, and not
// merely some element that happens to accept the call.
struct To_text {
  std::string operator () (const int value) const {
    return "int:" + std::to_string(value);
  }

  std::string operator () (const std::string& value) const {
    return "string:" + value;
  }

  std::string operator () (const std::vector< double>& value) const {
    return "vector:" + std::to_string(value.size());
  }
};

} // namespace

TEST(EnumTuple, ConstructsElementsOfDifferentTypes) {
  const Payload payload(42, "text", std::vector< double>{ 1.0, 2.0, 3.0 });

  EXPECT_EQ(payload.at< Field::INDEX>(), 42);
  EXPECT_EQ(payload.at< Field::LABEL>(), "text");
  EXPECT_EQ(payload.at< Field::WEIGHTS>(), (std::vector< double>{ 1.0, 2.0, 3.0 }));
}

TEST(EnumTuple, BracedListReachesTheVariadicConstructor) {
  const Payload payload { 42, "text", std::vector< double>{ 1.0, 2.0, 3.0 } };

  EXPECT_EQ(payload.at< Field::INDEX>(), 42);
  EXPECT_EQ(payload.at< Field::LABEL>(), "text");
  EXPECT_EQ(payload.at< Field::WEIGHTS>().size(), 3u);
}

TEST(EnumTuple, ParenthesizedFormBuildsTheElementItsOwnWay) {
  // A single argument to a vector element is a size, not a one-element list — the same reading Enum_array
  // gives it.
  const Payload payload(1, "x", std::vector< double>(3));

  EXPECT_EQ(payload.at< Field::WEIGHTS>(), (std::vector< double>{ 0.0, 0.0, 0.0 }));
}

TEST(EnumTuple, DefaultConstructionValueInitializesEveryElement) {
  const Payload payload;

  EXPECT_EQ(payload.at< Field::INDEX>(), 0);
  EXPECT_EQ(payload.at< Field::LABEL>(), "");
  EXPECT_TRUE(payload.at< Field::WEIGHTS>().empty());
}

TEST(EnumTuple, RuntimeReadWriteThroughCompileTimeKeys) {
  Payload payload;
  payload.at< Field::INDEX>() = 7;
  payload.at< Field::LABEL>() = "label";
  payload.at< Field::WEIGHTS>().push_back(0.5);

  EXPECT_EQ(payload.at< Field::INDEX>(), 7);
  EXPECT_EQ(payload.at< Field::LABEL>(), "label");
  EXPECT_EQ(payload.at< Field::WEIGHTS>(), (std::vector< double>{ 0.5 }));
}

TEST(EnumTuple, NameKeyedAccessReachesTheSameElement) {
  Payload payload(1, "text", std::vector< double>{});
  payload.at< Constexpr_string<"LABEL">>() += "!";

  EXPECT_EQ(payload.at< Field::LABEL>(), "text!");
  EXPECT_EQ(&payload.at< Constexpr_string<"INDEX">>(), &payload.at< Field::INDEX>());
}

TEST(EnumTuple, UnorderedDeclarationStillIndexesByUnderlyingValue) {
  // Enum_with_names< Axis> declares Z, X, Y; the arguments are still given in enum order X, Y, Z.
  const Coordinates coordinates(1, 2.5, 'z');

  EXPECT_EQ(coordinates.at< Axis::X>(), 1);
  EXPECT_EQ(coordinates.at< Axis::Y>(), 2.5);
  EXPECT_EQ(coordinates.at< Axis::Z>(), 'z');
}

TEST(EnumTuple, VariadicBuildsElementsInPlace) {
  Tracked::reset();

  const Enum_tuple< Axis, Tracked, int, char> tuple(std::string("value"), 1, 'c');

  EXPECT_EQ(Tracked::constructions, 1);
  EXPECT_EQ(Tracked::copies, 0);
  EXPECT_EQ(Tracked::moves, 0);
  EXPECT_EQ(tuple.at< Axis::X>().value, "value");
  EXPECT_EQ(tuple.at< Axis::Y>(), 1);
  EXPECT_EQ(tuple.at< Axis::Z>(), 'c');
}

TEST(EnumTuple, ConstructsANeitherCopyableNorMovableElement) {
  const Enum_tuple< Axis, Immovable, int, char> tuple(5, 1, 'c');

  EXPECT_EQ(tuple.at< Axis::X>().value, 5);
  EXPECT_EQ(tuple.at< Axis::Y>(), 1);
  EXPECT_EQ(tuple.at< Axis::Z>(), 'c');
}

TEST(EnumTuple, ConstructsMoveOnlyElements) {
  Enum_tuple< Axis, std::unique_ptr< int>, std::string, char> tuple(std::make_unique< int>(3), "text", 'c');

  ASSERT_TRUE(tuple.at< Axis::X>());
  EXPECT_EQ(*tuple.at< Axis::X>(), 3);
  EXPECT_EQ(tuple.at< Axis::Y>(), "text");
  EXPECT_EQ(tuple.at< Axis::Z>(), 'c');
}

TEST(EnumTuple, CopyConstructionIsNotHijackedByVariadicConstructor) {
  const Enum_tuple< Only_slot, std::any> source(std::any(42));
  const Enum_tuple< Only_slot, std::any> copy(source);

  ASSERT_TRUE(copy.at< Only_slot::SINGLE>().has_value());
  EXPECT_EQ(std::any_cast< int>(copy.at< Only_slot::SINGLE>()), 42);
}

TEST(EnumTuple, ElementWithoutComparisonOperatorsIsStillUsable) {
  Enum_tuple< Axis, Opaque, int, char> tuple(Opaque{ 3 }, 1, 'c');
  tuple.at< Axis::X>().value = 4;

  EXPECT_EQ(tuple.at< Axis::X>().value, 4);
  EXPECT_EQ(tuple.at< Axis::Y>(), 1);
  EXPECT_EQ(tuple.at< Axis::Z>(), 'c');
}

TEST(EnumTuple, ComparisonFollowsElementOrder) {
  const Coordinates first(1, 2.5, 'z');
  const Coordinates same(1, 2.5, 'z');
  const Coordinates greater(1, 3.5, 'a');

  EXPECT_EQ(first, same);
  EXPECT_NE(first, greater);
  EXPECT_LT(first, greater);
  EXPECT_GT(greater, first);
}

TEST(EnumTuple, VisitDispatchesToTheElementOfTheRuntimeKey) {
  const Payload payload(42, "text", std::vector< double>{ 1.0, 2.0 });

  EXPECT_EQ(payload.visit(Field::INDEX, To_text{}), "int:42");
  EXPECT_EQ(payload.visit(Field::LABEL, To_text{}), "string:text");
  EXPECT_EQ(payload.visit(Field::WEIGHTS, To_text{}), "vector:2");
}

TEST(EnumTuple, VisitYieldsAReferenceToTheElement) {
  Enum_tuple< Axis, int, int, int> counters(1, 2, 3);
  counters.visit(Axis::Y, [](int& value) -> int& { return value; }) += 10;

  EXPECT_EQ(counters.at< Axis::X>(), 1);
  EXPECT_EQ(counters.at< Axis::Y>(), 12);
  EXPECT_EQ(counters.at< Axis::Z>(), 3);
}

TEST(EnumTuple, VisitWorksAtCompileTime) {
  constexpr int doubled = [] {
    const Enum_tuple< Axis, int, int, int> counters(1, 2, 3);
    return counters.visit(Axis::Z, [](const int value) { return value * 2; });
  }();

  static_assert(doubled == 6);
  EXPECT_EQ(doubled, 6);
}

TEST(EnumTuple, VisitThrowsForUndeclaredEnumerator) {
  const Enum_tuple< Stage, int, std::string> stage(1, "text");

  EXPECT_THROW(stage.visit(Stage::UNDECLARED, To_text{}), std::out_of_range);
  EXPECT_EQ(stage.visit(Stage::FINISH, To_text{}), "string:text");
}

TEST(EnumTuple, VisitThrowsForValueCastFromArbitraryInteger) {
  const Payload payload(1, "text", std::vector< double>{});

  EXPECT_THROW(payload.visit(static_cast< Field>(42), To_text{}), std::out_of_range);
  EXPECT_THROW(payload.visit(static_cast< Field>(-1), To_text{}), std::out_of_range);
}

TEST(EnumTuple, ForEachVisitsEveryElementInEnumOrderWithItsKey) {
  const Payload payload(42, "text", std::vector< double>{ 1.0, 2.0 });

  std::vector< std::string> visited;
  payload.for_each([&visited](const auto& element, const auto key) {
    visited.push_back(std::string(Enum_with_names< Field>::get_name_by_value< decltype(key)::value>()) + '=' + To_text{}(element));
  });

  ASSERT_EQ(visited.size(), 3u);
  EXPECT_EQ(visited[0], "INDEX=int:42");
  EXPECT_EQ(visited[1], "LABEL=string:text");
  EXPECT_EQ(visited[2], "WEIGHTS=vector:2");
}

TEST(EnumTuple, ForEachCanModifyEveryElement) {
  Enum_tuple< Axis, int, int, int> counters(1, 2, 3);
  counters.for_each([](int& value, const auto /* key */) { value *= 10; });

  EXPECT_EQ(counters.at< Axis::X>(), 10);
  EXPECT_EQ(counters.at< Axis::Y>(), 20);
  EXPECT_EQ(counters.at< Axis::Z>(), 30);
}

TEST(EnumTuple, CopyAndMoveTheWholeContainer) {
  const Coordinates source(1, 2.5, 'z');
  const Coordinates copy(source);

  EXPECT_EQ(copy, source);

  Enum_tuple< Axis, std::unique_ptr< int>, std::string, char> movable(std::make_unique< int>(3), "text", 'c');
  const Enum_tuple< Axis, std::unique_ptr< int>, std::string, char> moved(std::move(movable));

  ASSERT_TRUE(moved.at< Axis::X>());
  EXPECT_EQ(*moved.at< Axis::X>(), 3);
  EXPECT_EQ(moved.at< Axis::Y>(), "text");
  EXPECT_EQ(moved.at< Axis::Z>(), 'c');
}

TEST(EnumTuple, StructuredBindingsWalkTheElementsInEnumOrder) {
  Payload payload(42, "text", std::vector< double>{ 1.0, 2.0 });
  auto& [index, label, weights] = payload;

  EXPECT_EQ(index, 42);
  EXPECT_EQ(label, "text");
  EXPECT_EQ(weights.size(), 2u);

  // The names bind to the elements themselves, not to copies of them.
  index = 7;
  label += "!";
  weights.clear();

  EXPECT_EQ(payload.at< Field::INDEX>(), 7);
  EXPECT_EQ(payload.at< Field::LABEL>(), "text!");
  EXPECT_TRUE(payload.at< Field::WEIGHTS>().empty());
}

TEST(EnumTuple, StructuredBindingsFollowEnumOrderAndNotDeclarationOrder) {
  // Enum_with_names< Axis> declares Z, X, Y; the bindings still come out as X, Y, Z.
  const Coordinates coordinates(1, 2.5, 'z');
  const auto& [x, y, z] = coordinates;

  EXPECT_EQ(x, 1);
  EXPECT_EQ(y, 2.5);
  EXPECT_EQ(z, 'z');
}

TEST(EnumTuple, StructuredBindingsOfAConstContainerAreConst) {
  const Payload payload(42, "text", std::vector< double>{ 1.0 });
  const auto& [index, label, weights] = payload;

  static_assert(std::same_as< decltype(index), const int>);
  static_assert(std::same_as< decltype(label), const std::string>);
  static_assert(std::same_as< decltype(weights), const std::vector< double>>);

  EXPECT_EQ(index, 42);
  EXPECT_EQ(label, "text");
  EXPECT_EQ(weights, (std::vector< double>{ 1.0 }));
}

TEST(EnumTuple, GetMovesOutOfAnRvalueContainer) {
  Enum_tuple< Axis, std::unique_ptr< int>, std::string, char> tuple(std::make_unique< int>(3), "text", 'c');
  const std::unique_ptr< int> taken = get< 0>(std::move(tuple));

  ASSERT_TRUE(taken);
  EXPECT_EQ(*taken, 3);
  EXPECT_FALSE(tuple.at< Axis::X>());
}

TEST(EnumTuple, SharesTheEnumDescriptionWithEnumArray) {
  // Both containers key off the same Enum_with_names specialization, now declared in one shared header.
  const Enum_array< Field, int> sizes(1, 2, 3);
  const Payload payload(1, "text", std::vector< double>{});

  static_assert(Payload::SIZE == Enum_array< Field, int>::SIZE);
  EXPECT_EQ(sizes.at< Field::LABEL>(), 2);
  EXPECT_EQ(payload.visit(Field::INDEX, To_text{}), "int:1");
  EXPECT_EQ(payload.at< Field::LABEL>(), "text");
}
