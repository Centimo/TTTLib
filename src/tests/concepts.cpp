#include "core/SRSW_cell.hpp"
#include "core/Thread.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

// ============================================================================
// Has_static_read (replaces CVS_HAS_STATIC_METHOD_DEFAULT) — SRSW_cell.hpp
// ============================================================================
namespace {

struct Doubler     { static int  read(const int& v) { return v * 2; } };
struct NoRead      {};
struct WrongReturn { static void read(const int&) {} };
struct WrongArg    { static int  read(const std::string&) { return 0; } };

} // namespace

static_assert(core::rcu::details::Has_static_read< Doubler, int >);
static_assert(!core::rcu::details::Has_static_read< NoRead, int >);
static_assert(!core::rcu::details::Has_static_read< WrongReturn, int >);  // void not convertible to int
static_assert(!core::rcu::details::Has_static_read< WrongArg, int >);     // not callable with const int&

// A cell parameterized with a valid reader type must compile.
using ReadCell = core::rcu::SRSW_cell< int, Doubler >;

TEST(HasStaticRead, DetectsReader) {
  EXPECT_TRUE((core::rcu::details::Has_static_read< Doubler, int >));
  EXPECT_FALSE((core::rcu::details::Has_static_read< NoRead, int >));
  EXPECT_FALSE((core::rcu::details::Has_static_read< WrongReturn, int >));
}

// ============================================================================
// Has_free_method (replaces CVS_HAS_INSTANCE_METHOD_DEFAULT) — Thread.hpp
// ============================================================================
namespace {

struct WithFree    { void free() {} };
struct WithoutFree {};
struct FreeWithArg { void free(int) {} };
struct StaticFree  { static void free() {} };

} // namespace

static_assert(core::details::Has_free_method< WithFree >);
static_assert(!core::details::Has_free_method< WithoutFree >);
static_assert(!core::details::Has_free_method< FreeWithArg >);  // needs a no-arg free()
static_assert(!core::details::Has_free_method< StaticFree >);   // must be a non-static member

TEST(HasFreeMethod, DetectsFree) {
  EXPECT_TRUE(core::details::Has_free_method< WithFree >);
  EXPECT_FALSE(core::details::Has_free_method< WithoutFree >);
  EXPECT_FALSE(core::details::Has_free_method< FreeWithArg >);
  EXPECT_FALSE(core::details::Has_free_method< StaticFree >);
}

// ============================================================================
// SRSW_cell smoke test (single-threaded)
// ============================================================================
TEST(SRSWCell, WriteThenRead) {
  using Cell = core::rcu::SRSW_cell< int >;
  Cell cell(0);

  {
    auto writer = cell.get_interface< Cell::Access_kind::WRITE >();
    writer->write(42);
  }
  {
    auto reader = cell.get_interface_const();
    EXPECT_EQ(reader->read(), 42);
  }
}

TEST(SRSWCell, ReadTransformsWithReaderType) {
  using Cell = core::rcu::SRSW_cell< int, Doubler >;
  Cell cell(21);
  auto reader = cell.get_interface_const();
  EXPECT_EQ(reader->read(), 42);  // Doubler::read doubles the stored value
}

// ============================================================================
// Thread: runs the executor, calls free() only when present
// ============================================================================
namespace {

struct FreeableWorker : core::Stop_flag {
  std::atomic<int>  ticks{0};
  std::atomic<bool> freed{false};
  void execute() { while (!is_stopped()) { ++ticks; std::this_thread::sleep_for(1ms); } }
  void free() { freed = true; }
};

struct PlainWorker : core::Stop_flag {
  std::atomic<int> ticks{0};
  void execute() { while (!is_stopped()) { ++ticks; std::this_thread::sleep_for(1ms); } }
};

} // namespace

TEST(Thread, RunsAndCallsFreeWhenPresent) {
  FreeableWorker worker;
  {
    core::Thread thread(worker);          // starts; dtor stops + joins + calls free()
    std::this_thread::sleep_for(20ms);
  }
  EXPECT_GT(worker.ticks.load(), 0);
  EXPECT_TRUE(worker.freed.load());
}

TEST(Thread, RunsWithoutFreeMethod) {
  PlainWorker worker;
  {
    core::Thread thread(worker);
    std::this_thread::sleep_for(20ms);
  }
  EXPECT_GT(worker.ticks.load(), 0);
}
