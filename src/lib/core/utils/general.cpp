#include "general.hpp"


namespace core::utils {

bool Atomic_flag::compare_exchange_expect_false() {
  bool expected = false;
  return std::atomic<bool>::compare_exchange_strong(expected, true);
}

bool Atomic_flag::compare_exchange_expect_true() {
  bool expected = true;
  return std::atomic<bool>::compare_exchange_strong(expected, false);
}

Index_range index_range(size_t range_size) {
  return Index_range{0, range_size};
}

} // namespace core::utils