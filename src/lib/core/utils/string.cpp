#include "string.hpp"

namespace core::utils::string {

std::string remove_password_from_address(std::string address) {
  auto position = address.find('@');
  if (position != std::string::npos) {
    address = address.substr(position + 1);
  }

  position = address.find("password=");
  if (position != std::string::npos) {
    address = address.substr(0, position);
  }

  return std::move(address);
}

bool transform_from_local_form(std::string& source, const std::string& target) {
  static const std::array<std::string, 2> local_addresses = {"127.0.0.1", "localhost"};
  bool result = false;
  for (const auto& local_address: local_addresses) {
    const auto find_result = source.find(local_address);
    if (find_result != std::string::npos) {
      source.replace(find_result, local_address.size(), target);
      result = true;
    }
  }

  return result;
}

} // namespace core::utils::string