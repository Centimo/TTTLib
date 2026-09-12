#include "core/utils/time.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <utility>

using namespace core::utils::time;

namespace {

// The injected clock stands in for system_clock, so readiness is decided by the test, not by wall time.
TEST(Timer, MoveKeepsInjectedClockAndRecordedKeys) {
  auto now = std::chrono::system_clock::time_point{};
  Timer< int> source(std::chrono::seconds(10), [&now] { return now; });
  EXPECT_TRUE(source.is_ready(1));

  Timer< int> moved(std::move(source));
  EXPECT_FALSE(moved.check_readiness(1));

  now += std::chrono::seconds(11);
  EXPECT_TRUE(moved.check_readiness(1));
  EXPECT_TRUE(moved.is_ready(1));
  EXPECT_FALSE(moved.is_ready(1));
}

} // namespace
