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

TEST(FixedString, ViewAndCompare) {
  EXPECT_EQ(Constexpr_string<"teapot">::view(), "teapot");
  EXPECT_TRUE((meta::Less< Constexpr_string<"a">, Constexpr_string<"b"> >::value));
  EXPECT_FALSE((meta::Less< Constexpr_string<"b">, Constexpr_string<"a"> >::value));
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
