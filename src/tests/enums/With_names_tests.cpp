#include "core/utils/enums/With_names.hpp"

#include <gtest/gtest.h>

using namespace core::utils::enums;
using namespace core::utils::string;

// ---- With_names with deliberately UNSORTED names and values ----
enum class Http { not_found = 404, ok = 200, teapot = 418, moved = 301 };

namespace core::utils::enums {
template<>
class With_names< Http > : public With_names_base<
  Named_value< Http::not_found, Constexpr_string<"not_found"> >,
  Named_value< Http::ok,        Constexpr_string<"ok"> >,
  Named_value< Http::teapot,    Constexpr_string<"teapot"> >,
  Named_value< Http::moved,     Constexpr_string<"moved"> >
> {};
} // namespace core::utils::enums

using Codes = With_names< Http >;

static_assert(Codes::get_value_by_name< Constexpr_string<"ok"> >() == Http::ok);
static_assert(Codes::get_name_by_value< Http::teapot >() == "teapot");
static_assert(Codes::SIZE == 4);

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
