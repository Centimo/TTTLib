#include "MRSW_cell.hpp"

#include <ranges>


namespace core::rcu {

std::optional< uint8_t > String_uint_multi_index_map_MRSW::Multi_index_map::get_uint_by_string(std::string_view string) const {
  const auto find_result = _uints_by_strings.find(string);
  if (find_result == _uints_by_strings.end()) {
    return std::nullopt;
  }
  else {
    return find_result->second;
  }
}

String_uint_multi_index_map_MRSW::Optional_reference_string
  String_uint_multi_index_map_MRSW::Multi_index_map::get_string_by_uint(uint8_t index) const
{
  if (index >= _strings.size()) {
    return std::nullopt;
  }
  else {
    return _strings[index];
  }
}

String_uint_multi_index_map_MRSW::Optional_reference_string
  String_uint_multi_index_map_MRSW::Multi_index_map::swap_string_into_new_uint(
    std::string_view string,
    uint8_t new_index
  )
{
  const auto old_index = get_uint_by_string(string);
  if (!old_index || *old_index == new_index) {
    return std::nullopt;
  }

  const auto old_string = get_string_by_uint(new_index);
  if (!old_string) {
    return std::nullopt;
  }

  _strings[*old_index] = *old_string;
  _strings[new_index] = string;

  _uints_by_strings[_strings[new_index]] = new_index;
  _uints_by_strings[_strings[*old_index]] = *old_index;

  return old_string;
}

String_uint_multi_index_map_MRSW::Multi_index_map::Multi_index_map(std::vector< std::string > strings)
  : _strings(std::move(strings))
{
  for (const auto& [index, value]: std::views::enumerate(_strings)) {
    _uints_by_strings[value] = index;
  }
}

String_uint_multi_index_map_MRSW::Multi_index_map&
  String_uint_multi_index_map_MRSW::Multi_index_map::operator=(
    const String_uint_multi_index_map_MRSW::Multi_index_map& copy
  ) {
  _strings = copy._strings;
  for (const auto& [index, value]: std::views::enumerate(_strings)) {
    _uints_by_strings[value] = index;
  }

  return *this;
}

String_uint_multi_index_map_MRSW::String_uint_multi_index_map_MRSW(std::vector< std::string > strings)
  : _map(std::move(strings))
{}

String_uint_multi_index_map_MRSW::String_uint_multi_index_map_MRSW(std::vector< std::string_view > strings)
  : _map(std::move(std::vector< std::string >(strings.begin(), strings.end())))
{}

} // namespace core::rcu