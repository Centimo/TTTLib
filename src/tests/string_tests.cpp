#include "core/utils/string.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace core::utils::string;
namespace meta = core::utils::meta;

// ---- fixed_string / Constexpr_string ----
static_assert(fixed_string("abc").view() == "abc");
static_assert(Constexpr_string<"abc">::view() == "abc");
static_assert(Constexpr_string<"">::view().empty());
static_assert(meta::Less< Constexpr_string<"apple">, Constexpr_string<"banana"> >::value);
static_assert(!meta::Less< Constexpr_string<"banana">, Constexpr_string<"apple"> >::value);

// ---- Enum_with_names with deliberately UNSORTED names and values ----
enum class Http { not_found = 404, ok = 200, teapot = 418, moved = 301 };

namespace core::utils::string {
template<>
class Enum_with_names< Http > : public Enum_with_names_base<
  Named_enum_value< Http::not_found, Constexpr_string<"not_found"> >,
  Named_enum_value< Http::ok,        Constexpr_string<"ok"> >,
  Named_enum_value< Http::teapot,    Constexpr_string<"teapot"> >,
  Named_enum_value< Http::moved,     Constexpr_string<"moved"> >
> {};
} // namespace core::utils::string

using Codes = Enum_with_names< Http >;

static_assert(Codes::get_value_by_name< Constexpr_string<"ok"> >() == Http::ok);
static_assert(Codes::get_name_by_value< Http::teapot >() == "teapot");
static_assert(Codes::SIZE == 4);

TEST(FixedString, ViewAndCompare) {
  EXPECT_EQ(Constexpr_string<"teapot">::view(), "teapot");
  EXPECT_TRUE((meta::Less< Constexpr_string<"a">, Constexpr_string<"b"> >::value));
  EXPECT_FALSE((meta::Less< Constexpr_string<"b">, Constexpr_string<"a"> >::value));
}

TEST(EnumWithNames, CompileTimeNameToValue) {
  EXPECT_EQ((Codes::get_value_by_name< Constexpr_string<"moved"> >()),     Http::moved);
  EXPECT_EQ((Codes::get_value_by_name< Constexpr_string<"not_found"> >()), Http::not_found);
  EXPECT_EQ((Codes::get_value_by_name< Constexpr_string<"teapot"> >()),    Http::teapot);
}

TEST(EnumWithNames, CompileTimeValueToName) {
  EXPECT_EQ((Codes::get_name_by_value< Http::ok >()),        "ok");
  EXPECT_EQ((Codes::get_name_by_value< Http::not_found >()), "not_found");
  EXPECT_EQ((Codes::get_name_by_value< Http::moved >()),     "moved");
}

TEST(EnumWithNames, RuntimeLookups) {
  ASSERT_TRUE(Codes::get_value_by_name("teapot").has_value());
  EXPECT_EQ(*Codes::get_value_by_name("teapot"), Http::teapot);
  EXPECT_EQ(*Codes::get_value_by_name("moved"),  Http::moved);
  EXPECT_FALSE(Codes::get_value_by_name("gone").has_value());

  ASSERT_TRUE(Codes::get_name_by_value(Http::not_found).has_value());
  EXPECT_EQ(*Codes::get_name_by_value(Http::not_found), "not_found");
  EXPECT_EQ(*Codes::get_name_by_value(Http::ok),        "ok");
}

TEST(EnumWithNames, Size) {
  EXPECT_EQ(Codes::SIZE, 4u);
}

// ---- free functions from string.cpp ----
TEST(StringUtils, RemovePasswordFromAddress) {
  EXPECT_EQ(remove_password_from_address("user:pw@host:5432/db?password=secret"),
            "host:5432/db?");
  EXPECT_EQ(remove_password_from_address("plainhost"), "plainhost");
}

TEST(StringUtils, TransformFromLocalForm) {
  std::string local = "http://127.0.0.1:8080";
  EXPECT_TRUE(transform_from_local_form(local, "example.com"));
  EXPECT_EQ(local, "http://example.com:8080");

  std::string remote = "http://remote:8080";
  EXPECT_FALSE(transform_from_local_form(remote, "example.com"));
  EXPECT_EQ(remote, "http://remote:8080");
}
