#include "time.hpp"

namespace core::utils::time {

bool greater_ms(std::chrono::milliseconds first, std::chrono::milliseconds second) {
  return first > second;
}

bool greater_s(std::chrono::seconds first, std::chrono::seconds second) {
  return first > second;
}

bool less_ms(std::chrono::milliseconds first, std::chrono::milliseconds second) {
  return first < second;
}

bool less_s(std::chrono::seconds first, std::chrono::seconds second) {
  return first < second;
}

} // namespace core::utils::time