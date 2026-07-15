#pragma once

#include <chrono>
#include <format>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>


namespace core::utils::time {

template <class Rep, class Period>
std::chrono::milliseconds cast_to_ms(const std::chrono::duration<Rep, Period>& duration) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(duration);
}

template <class Rep, class Period>
std::chrono::seconds cast_to_s(const std::chrono::duration<Rep, Period>& duration) {
  return std::chrono::duration_cast<std::chrono::seconds>(duration);
}

template< class Clock = std::chrono::system_clock >
std::chrono::milliseconds ms_since(std::chrono::time_point< Clock > time_point) {
  return cast_to_ms(Clock::now() - time_point);
}

template <class Clock = std::chrono::system_clock>
std::chrono::seconds seconds_since(std::chrono::time_point< Clock > time_point) {
  return cast_to_s(Clock::now() - time_point);;
}

bool greater_ms(std::chrono::milliseconds first, std::chrono::milliseconds second);
bool greater_s(std::chrono::seconds first, std::chrono::seconds second);
bool less_ms(std::chrono::milliseconds first, std::chrono::milliseconds second);
bool less_s(std::chrono::seconds first, std::chrono::seconds second);


inline constexpr std::string_view DEFAULT_TIME_FORMAT = "%Y-%m-%d %H:%M:%S";

template <class Clock = std::chrono::system_clock>
std::optional< std::chrono::time_point< Clock > > from_string(
  const std::string& text,
  const std::string& format = std::string(DEFAULT_TIME_FORMAT)
) {
  std::istringstream input_stream{text };
  std::chrono::time_point< Clock > result;
  input_stream >> std::chrono::parse(format, result);
  if (input_stream.fail()) {
    return {};
  }

  return result;
}

template <class Clock = std::chrono::system_clock>
std::string to_string(
  std::chrono::time_point< Clock > time,
  const std::string& format = std::string(DEFAULT_TIME_FORMAT)
) {
  return std::vformat("{:" + format + "}", std::make_format_args(time));
}

template<
  class Index,
  bool with_cleaning = true,
  class Time_point = std::chrono::system_clock::time_point,
  class Time_duration = std::chrono::system_clock::duration
>
  requires std::default_initializable< std::unordered_map< Index, Time_point > >
class Timer {
 public:
  using Function_now_type = std::function< std::chrono::system_clock::time_point () >;

  Timer(Time_duration duration, std::optional< Function_now_type > function_now = std::nullopt)
    : _duration(duration), _function_now(function_now) {}

  Timer(Timer&& movable)
    : _duration(movable._duration)
    , _function_now(movable._function_function)
    , _objects(std::move(movable._objects))
  {};

  Timer(const Timer&) = delete;

  bool is_ready(Index key) {
    const auto now = Timer::now();
    const auto find_result = _objects.find(key);
    if (find_result == _objects.end()) {
      _objects.emplace(key, now);

      if constexpr (with_cleaning) {
        std::erase_if(
          _objects,
          [this, &now](const auto& item) {
            return Time_duration(now - item.second) > _duration;
          }
        );
      }

      return true;
    }

    if (Time_duration(now - find_result->second) > _duration) {
      find_result->second = now;
      return true;
    }

    return false;
  }

  bool check_readiness(Index key) const {
    const auto now = Timer::now();
    const auto find_result = _objects.find(key);

    if (find_result == _objects.end()) {
      return true;
    }

    if (Time_duration(now - find_result->second) > _duration) {
      return true;
    }

    return false;
  }

 private:
  Time_point now() const {
    if (!_function_now) {
      if constexpr (std::is_same_v< Time_point, std::chrono::system_clock::time_point >) {
        return std::chrono::system_clock::now();
      }
      else {
        throw std::runtime_error("Function 'now()' doesn't available for provided time_point type");
      }
    }
    else {
      return (*_function_now)();
    }
  }

 private:
  const Time_duration _duration;
  const std::optional< Function_now_type > _function_now;

  std::unordered_map< Index, Time_point > _objects;
};

} // namespace core::utils::time


