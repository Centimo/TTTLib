#include "core/utils/enum_variant.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace core::utils;
using namespace core::utils::string;

// ---- dense enum with names declared in enum order ----
enum class Kind { NUMBER, TEXT, WEIGHTS };

namespace core::utils::string {
template<>
class Enum_with_names< Kind> : public Enum_with_names_base<
  Named_enum_value< Kind::NUMBER,  Constexpr_string<"NUMBER">>,
  Named_enum_value< Kind::TEXT,    Constexpr_string<"TEXT">>,
  Named_enum_value< Kind::WEIGHTS, Constexpr_string<"WEIGHTS">>
> {};
} // namespace core::utils::string

// ---- dense enum with names declared out of enum order ----
enum class Slot { FIRST, SECOND, THIRD };

namespace core::utils::string {
template<>
class Enum_with_names< Slot> : public Enum_with_names_base<
  Named_enum_value< Slot::THIRD,  Constexpr_string<"THIRD">>,
  Named_enum_value< Slot::FIRST,  Constexpr_string<"FIRST">>,
  Named_enum_value< Slot::SECOND, Constexpr_string<"SECOND">>
> {};
} // namespace core::utils::string

using Message = Enum_variant< Kind, int, std::string, std::vector< double>>;
using Codes = Enum_variant< Kind, int, char, double>;

// The same type in two positions: it can only be reached through its key.
using Slots = Enum_variant< Slot, int, std::string, int>;

// ---- constexpr use ----
constexpr Codes CODES(in_place_key< Kind::TEXT>, 'z');
static_assert(Codes::SIZE == 3);
static_assert(CODES.active_key() == Kind::TEXT);
static_assert(CODES.holds< Kind::TEXT>());
static_assert(!CODES.holds< Kind::NUMBER>());
static_assert(CODES.at< Kind::TEXT>() == 'z');
static_assert(CODES.at< Constexpr_string<"TEXT">>() == 'z');
static_assert(Codes().active_key() == Kind::NUMBER);

// ---- element types ----
static_assert(std::same_as< Message::Element< Kind::NUMBER>, int>);
static_assert(std::same_as< Message::Element< Kind::TEXT>, std::string>);
static_assert(std::same_as< Message::Element< Kind::WEIGHTS>, std::vector< double>>);
static_assert(std::same_as< decltype(std::declval< Message&>().at< Kind::TEXT>()), std::string&>);
static_assert(std::same_as< decltype(std::declval< const Message&>().at< Kind::TEXT>()), const std::string&>);

// ---- which alternative an argument reaches without a key ----
static_assert(std::constructible_from< Message, int>);
static_assert(std::constructible_from< Message, std::string>);
static_assert(std::constructible_from< Message, const char*>);
static_assert(std::constructible_from< Message, std::vector< double>>);
static_assert(!std::constructible_from< Message, double>);   // narrowing into the int alternative

// A type standing in two positions leaves the choice ambiguous, so only its key can name it; the type
// standing in one is unaffected.
static_assert(!std::constructible_from< Slots, int>);
static_assert(std::constructible_from< Slots, std::string>);
static_assert(std::constructible_from< Slots, In_place_key< Slot::THIRD>, int>);

// Naming the key does not buy a silent truncation the keyless constructor rejects.
static_assert(!std::constructible_from< Message, In_place_key< Kind::NUMBER>, double>);
static_assert(std::constructible_from< Message, In_place_key< Kind::NUMBER>, int>);

namespace {

// Throws on demand while being built, moves without throwing: the combination that tells whether a failed
// emplace left the previous element alone.
struct Fragile {
  std::string value;

  Fragile(std::string text, const bool should_throw) : value(std::move(text)) {
    if (should_throw) {
      throw std::runtime_error("Fragile refused to be built");
    }
  }

  Fragile(const Fragile&) = default;
  Fragile(Fragile&&) noexcept = default;
};

using Fragile_holder = Enum_variant< Kind, int, Fragile, std::vector< double>>;

// Copyable and assignable, but its copy constructor throws on demand: this is what an assignment between
// two containers has to survive without losing the element it already held.
struct Fragile_copy {
  std::string value;
  bool throws_on_copy = false;

  Fragile_copy() = default;
  explicit Fragile_copy(std::string text, const bool should_throw)
    : value(std::move(text))
    , throws_on_copy(should_throw)
  {}

  Fragile_copy(const Fragile_copy& other) : value(other.value), throws_on_copy(other.throws_on_copy) {
    if (throws_on_copy) {
      throw std::runtime_error("Fragile_copy refused to be copied");
    }
  }

  Fragile_copy(Fragile_copy&&) noexcept = default;
  Fragile_copy& operator = (const Fragile_copy&) = default;
  Fragile_copy& operator = (Fragile_copy&&) noexcept = default;
};

using Fragile_copy_holder = Enum_variant< Kind, int, Fragile_copy, std::vector< double>>;

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

TEST(EnumVariant, DefaultConstructionActivatesTheFirstEnumerator) {
  const Message message;

  EXPECT_EQ(message.active_key(), Kind::NUMBER);
  EXPECT_TRUE(message.holds< Kind::NUMBER>());
  EXPECT_EQ(message.at< Kind::NUMBER>(), 0);
}

TEST(EnumVariant, KeyChoosesTheAlternativeAndArgumentsBuildIt) {
  const Message weights(in_place_key< Kind::WEIGHTS>, 3, 1.5);

  EXPECT_EQ(weights.active_key(), Kind::WEIGHTS);
  EXPECT_EQ(weights.at< Kind::WEIGHTS>(), (std::vector< double>{ 1.5, 1.5, 1.5 }));
}

TEST(EnumVariant, ArgumentAloneReachesAUniqueAlternative) {
  const Message number(42);
  const Message text("text");
  const Message weights(std::vector< double>{ 1.0, 2.0 });

  EXPECT_EQ(number.active_key(), Kind::NUMBER);
  EXPECT_EQ(number.at< Kind::NUMBER>(), 42);
  EXPECT_EQ(text.active_key(), Kind::TEXT);
  EXPECT_EQ(text.at< Kind::TEXT>(), "text");
  EXPECT_EQ(weights.active_key(), Kind::WEIGHTS);
  EXPECT_EQ(weights.at< Kind::WEIGHTS>().size(), 2u);
}

TEST(EnumVariant, ARepeatedTypeIsReachedThroughItsKey) {
  const Slots first(in_place_key< Slot::FIRST>, 1);
  const Slots third(in_place_key< Slot::THIRD>, 3);
  const Slots second("text");

  EXPECT_EQ(first.active_key(), Slot::FIRST);
  EXPECT_EQ(first.at< Slot::FIRST>(), 1);
  EXPECT_EQ(third.active_key(), Slot::THIRD);
  EXPECT_EQ(third.at< Slot::THIRD>(), 3);
  EXPECT_EQ(second.active_key(), Slot::SECOND);
  EXPECT_EQ(second.at< Slot::SECOND>(), "text");
}

TEST(EnumVariant, CheckedAccessThrowsForAnInactiveKey) {
  const Message message("text");

  EXPECT_EQ(message.at< Kind::TEXT>(), "text");
  EXPECT_THROW((void) message.at< Kind::NUMBER>(), std::bad_variant_access);
  EXPECT_THROW((void) message.at< Kind::WEIGHTS>(), std::bad_variant_access);
}

TEST(EnumVariant, UncheckedAccessYieldsNothingForAnInactiveKey) {
  Message message("text");

  EXPECT_FALSE(message.at_if< Kind::NUMBER>());
  EXPECT_FALSE(message.at_if< Kind::WEIGHTS>());

  ASSERT_TRUE(message.at_if< Kind::TEXT>());
  *message.at_if< Kind::TEXT>() += "!";
  EXPECT_EQ(message.at< Kind::TEXT>(), "text!");

  const Message& constant = message;
  ASSERT_TRUE(constant.at_if< Kind::TEXT>());
  EXPECT_EQ(*constant.at_if< Kind::TEXT>(), "text!");
  EXPECT_FALSE(constant.at_if< Kind::NUMBER>());
}

TEST(EnumVariant, EmplaceReplacesTheActiveElement) {
  Message message(42);
  const std::string& text = message.emplace< Kind::TEXT>("text");

  EXPECT_EQ(text, "text");
  EXPECT_EQ(message.active_key(), Kind::TEXT);
  EXPECT_EQ(message.at< Kind::TEXT>(), "text");

  message.emplace< Kind::WEIGHTS>(2, 0.5);
  EXPECT_EQ(message.active_key(), Kind::WEIGHTS);
  EXPECT_EQ(message.at< Kind::WEIGHTS>(), (std::vector< double>{ 0.5, 0.5 }));
}

TEST(EnumVariant, AThrowingEmplaceLeavesThePreviousElementInPlace) {
  Fragile_holder holder(42);

  EXPECT_THROW(holder.emplace< Kind::TEXT>(std::string("text"), true), std::runtime_error);

  EXPECT_EQ(holder.active_key(), Kind::NUMBER);
  EXPECT_TRUE(holder.holds< Kind::NUMBER>());
  EXPECT_EQ(holder.at< Kind::NUMBER>(), 42);

  holder.emplace< Kind::TEXT>(std::string("text"), false);
  EXPECT_EQ(holder.active_key(), Kind::TEXT);
  EXPECT_EQ(holder.at< Kind::TEXT>().value, "text");
}

TEST(EnumVariant, EmplaceFromTheContainersOwnElementKeepsTheValue) {
  // The argument refers to the element being replaced: it has to be consumed before that element dies.
  Message message("text");
  message.emplace< Kind::TEXT>(message.at< Kind::TEXT>());

  EXPECT_EQ(message.active_key(), Kind::TEXT);
  EXPECT_EQ(message.at< Kind::TEXT>(), "text");

  // An element whose copy cannot throw is the tempting case for building in place, and the one where
  // reading the destroyed element would go unnoticed the longest: the count says the copy came from a
  // living object.
  Enum_variant< Kind, int, std::shared_ptr< int>, std::vector< double>> holder(std::make_shared< int>(7));
  holder.emplace< Kind::TEXT>(holder.at< Kind::TEXT>());

  ASSERT_TRUE(holder.at< Kind::TEXT>());
  EXPECT_EQ(*holder.at< Kind::TEXT>(), 7);
  EXPECT_EQ(holder.at< Kind::TEXT>().use_count(), 1);
}

TEST(EnumVariant, EmplaceTellsRepeatedTypesApartByKey) {
  Slots slots(in_place_key< Slot::FIRST>, 1);
  slots.emplace< Slot::THIRD>(3);

  EXPECT_EQ(slots.active_key(), Slot::THIRD);
  EXPECT_EQ(slots.at< Slot::THIRD>(), 3);
  EXPECT_FALSE(slots.at_if< Slot::FIRST>());

  slots.emplace< Slot::FIRST>(1);
  EXPECT_EQ(slots.active_key(), Slot::FIRST);
  EXPECT_EQ(slots.at< Slot::FIRST>(), 1);
  EXPECT_FALSE(slots.at_if< Slot::THIRD>());
}

TEST(EnumVariant, AThrowingAssignmentLeavesThePreviousElementInPlace) {
  Fragile_copy_holder target(42);
  const Fragile_copy_holder source(in_place_key< Kind::TEXT>, std::string("text"), true);

  EXPECT_THROW(target = source, std::runtime_error);

  EXPECT_EQ(target.active_key(), Kind::NUMBER);
  EXPECT_TRUE(target.holds< Kind::NUMBER>());
  EXPECT_EQ(target.at< Kind::NUMBER>(), 42);
}

TEST(EnumVariant, AssignmentReplacesTheActiveElement) {
  Message message(42);
  message = Message("text");

  EXPECT_EQ(message.active_key(), Kind::TEXT);
  EXPECT_EQ(message.at< Kind::TEXT>(), "text");
}

TEST(EnumVariant, VisitReceivesTheLiveElementAndItsKey) {
  const Message number(42);
  const Message text("text");
  const Message weights(std::vector< double>{ 1.0, 2.0 });

  const auto describe = [](const auto& element, const auto key) {
    return std::string(Enum_with_names< Kind>::get_name_by_value< decltype(key)::value>())
           + '=' + To_text{}(element);
  };

  EXPECT_EQ(number.visit(describe), "NUMBER=int:42");
  EXPECT_EQ(text.visit(describe), "TEXT=string:text");
  EXPECT_EQ(weights.visit(describe), "WEIGHTS=vector:2");
}

TEST(EnumVariant, VisitReachesTheElementForWriting) {
  Message message(42);
  message.visit([](auto& element, const auto /* key */) {
    if constexpr (std::same_as< std::remove_reference_t< decltype(element)>, int>) {
      element += 1;
    }
  });

  EXPECT_EQ(message.at< Kind::NUMBER>(), 43);
}

TEST(EnumVariant, VisitWorksAtCompileTime) {
  constexpr char stored = [] {
    const Codes codes(in_place_key< Kind::TEXT>, 'z');
    return codes.visit([](const auto element, const auto /* key */) { return static_cast< char>(element); });
  }();

  static_assert(stored == 'z');
  EXPECT_EQ(stored, 'z');
}

TEST(EnumVariant, UnorderedDeclarationStillDiscriminatesByUnderlyingValue) {
  // Enum_with_names< Slot> declares THIRD, FIRST, SECOND; the positions still follow the enum.
  const Slots second("text");

  EXPECT_EQ(second.active_key(), Slot::SECOND);
  EXPECT_EQ(second.at< Constexpr_string<"SECOND">>(), "text");
}

TEST(EnumVariant, HoldsAMoveOnlyAlternative) {
  Enum_variant< Kind, int, std::unique_ptr< int>, std::vector< double>> holder(std::make_unique< int>(3));

  ASSERT_TRUE(holder.holds< Kind::TEXT>());
  ASSERT_TRUE(holder.at< Kind::TEXT>());
  EXPECT_EQ(*holder.at< Kind::TEXT>(), 3);

  const auto taken = std::move(holder.at< Kind::TEXT>());
  ASSERT_TRUE(taken);
  EXPECT_EQ(*taken, 3);
}

TEST(EnumVariant, ComparisonOrdersByKeyFirst) {
  const Message number(42);
  const Message same(42);
  const Message greater_number(43);
  const Message text("text");

  EXPECT_EQ(number, same);
  EXPECT_NE(number, greater_number);
  EXPECT_LT(number, greater_number);
  EXPECT_LT(greater_number, text);   // NUMBER comes before TEXT, whatever the elements are
}
