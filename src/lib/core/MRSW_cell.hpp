#pragma once

#include "utils/general.hpp"
#include "utils/meta.hpp"
#include "MPSC_queue.hpp"

#include <boost/scope_exit.hpp>

#include <thread>
#include <future>


namespace core::rcu {

template <class T, size_t max_procedures_processed_by_writer = 2 >
  requires std::is_copy_constructible_v< T > && std::default_initializable< T >
class MRSW_cell {
  static constexpr unsigned char _cells_number = 3;
  static constexpr unsigned char _max_tries = 100;
  static constexpr unsigned char _max_procedures_processed_by_writer = max_procedures_processed_by_writer;

  struct Cell {
    explicit Cell(const T& value) : _value(value) {}
    explicit Cell(T&& value) requires std::is_move_assignable_v< T >: _value(std::move(value)) {}

    explicit Cell() requires std::is_default_constructible_v< T > = default;
    Cell(const Cell&) = default;
    Cell(Cell&&) = delete;

    Cell& operator=(const Cell& copy) requires std::is_copy_assignable_v< T > {
      _value = copy._value;
      _readers_number.store(copy._readers_number.load());
      return *this;
    }

    T _value;
    mutable std::atomic<unsigned char> _readers_number;
  };

 public:
  explicit MRSW_cell(const T& default_value) {
    _cells.fill(default_value);
  }

  template <class... Arguments>
  explicit MRSW_cell(Arguments&&... arguments)
    requires std::is_constructible_v< T, Arguments... > && std::is_copy_assignable_v< T >
  {
    _cells.fill(Cell(T(std::forward<Arguments>(arguments)...)));
  }

  explicit MRSW_cell() requires std::is_default_constructible_v< T > = default;
  MRSW_cell(const MRSW_cell&) = delete;
  MRSW_cell(MRSW_cell&&) = delete;


  T read() const requires std::is_copy_assignable_v< T > {
    for (size_t i = 0; i < _max_tries; ++i) {
      const auto read_cell_index = _last_written_cell_index.load();
      const auto& read_cell = _cells[read_cell_index];
      read_cell._readers_number++;
      BOOST_SCOPE_EXIT_ALL(&read_cell) {
        read_cell._readers_number--;
      };

      if (read_cell_index == _current_writing_cell_index.load()) {
        continue;
      }

      return read_cell._value;
    }

    throw std::runtime_error("Can't read cell");
  }

  T process_queue_then_read() requires std::is_copy_assignable_v< T > {
    if (
      !_procedures_queue.empty() && _current_writing_cell_index.load() == _cells_number
    ) {
      unsigned char expected_value = _cells_number;
      const auto cell_index_to_write = (_last_written_cell_index.load() + 1) % _cells_number;
      if (_current_writing_cell_index.compare_exchange_strong(expected_value, cell_index_to_write)) {
        process_queue(cell_index_to_write);
        _last_written_cell_index.store(cell_index_to_write);
        _current_writing_cell_index.store(_cells_number);
      }
    }

    return read();
  }

  template< bool wait_readers_to_leave = true, class Procedure = void >
    requires
      (std::is_invocable_r_v< void, Procedure, T& >)
      //&& std::is_copy_assignable_v< Procedure > // copy lambda into std::function
  void write(Procedure&& procedure) {
    unsigned char expected_value = _cells_number;
    const auto cell_index_to_write = (_last_written_cell_index.load() + 1) % _cells_number;
    if (!_current_writing_cell_index.compare_exchange_strong(expected_value, cell_index_to_write)) {
#define CVS_MRSW_CELL_ENQUEUE_THEN_RETURN                                                                             \
      if (_procedures_queue.size() > _max_tries) {                                                                    \
        throw std::runtime_error("Overflowed procedures queue");                                                      \
      }                                                                                                               \
                                                                                                                      \
      _procedures_queue.enqueue(std::move(procedure));                                                                \
      return

      CVS_MRSW_CELL_ENQUEUE_THEN_RETURN;
    }

    for (size_t i = 0; i < _max_tries; ++i) {
      if (_cells[cell_index_to_write]._readers_number.load() != 0) {
        if constexpr (wait_readers_to_leave) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          continue;
        }
        else {
          CVS_MRSW_CELL_ENQUEUE_THEN_RETURN;
        }
      }

      _cells[cell_index_to_write]._value = read();

      if (_procedures_queue.size() == 0) {
        procedure(_cells[cell_index_to_write]._value);
      }
      else {
        _procedures_queue.enqueue(std::move(procedure));
        process_queue(cell_index_to_write);
      }

      _last_written_cell_index.store(cell_index_to_write);
      _current_writing_cell_index.store(_cells_number);
      return;
    }

    throw std::runtime_error("Can't write cell");
  }

  template< bool wait_readers_to_leave = true > requires std::is_move_assignable_v< T >
  void write(T&& value) {
    write< wait_readers_to_leave >(
      [value = std::move(value)] (T& current_value) {
        current_value = std::move(value);
      }
    );
  }

  template< bool wait_readers_to_leave = true > requires std::is_copy_assignable_v< T >
  void write(const T& value) {
    write< wait_readers_to_leave >(
      [value = std::move(value)] (T& current_value) {
        current_value = value;
      }
    );
  }

 private:
  void process_queue(size_t cell_index_to_write) {
    for (const auto _: core::utils::index_range(_max_procedures_processed_by_writer)) {
      if (
        auto dequeued_procedure = _procedures_queue.dequeue();
        dequeued_procedure
      ) {
        dequeued_procedure.value()(_cells[cell_index_to_write]._value);
      }
      else {
        break;
      }
    }
  }


 private:
  std::array< Cell, _cells_number > _cells;
  //mutable std::atomic_flag _is_writing = false;
  mutable std::atomic<unsigned char> _last_written_cell_index = 0;
  mutable std::atomic<unsigned char> _current_writing_cell_index = _cells_number;
  MPSC_queue< std::function<void (T&)> > _procedures_queue;
};


class String_uint_multi_index_map_MRSW {
  using Optional_reference_string = std::optional< std::reference_wrapper< const std::string > >;

  class Multi_index_map {
   public:
    explicit Multi_index_map() = default;
    explicit Multi_index_map(std::vector< std::string > strings);

    Multi_index_map& operator=(const Multi_index_map&);

    std::optional< uint8_t > get_uint_by_string(std::string_view string) const;
    Optional_reference_string get_string_by_uint(uint8_t index) const;
    Optional_reference_string swap_string_into_new_uint(std::string_view string, uint8_t new_index);

   private:
    std::vector< std::string > _strings;
    std::unordered_map< std::string_view, uint8_t > _uints_by_strings;
  };

  using Future_optional_string = std::future< std::optional< std::string > >;

 public:
  explicit String_uint_multi_index_map_MRSW() = default;
  explicit String_uint_multi_index_map_MRSW(std::vector< std::string > strings);
  explicit String_uint_multi_index_map_MRSW(std::vector< std::string_view > strings);

  std::optional< uint8_t > get_uint_by_string_fast(std::string_view string) const {
    const auto strings = _map.read();
    return strings.get_uint_by_string(string);
  }
  std::optional< uint8_t > get_uint_by_string(std::string_view string) {
    const auto strings = _map.process_queue_then_read();
    return strings.get_uint_by_string(string);
  }

  std::optional< std::string > get_string_by_uint_fast(uint8_t index) const  {
    const auto strings = _map.read();
    const auto result = strings.get_string_by_uint(index);
    return result ? std::make_optional< std::string >(*result) : std::nullopt;
  }

  std::optional< std::string > get_string_by_uint(uint8_t index) {
    const auto strings = _map.process_queue_then_read();
    const auto result = strings.get_string_by_uint(index);
    return result ? std::make_optional< std::string >(*result) : std::nullopt;
  }

  template< bool wait_readers_to_leave = true >
  Future_optional_string swap_string_into_new_uint(std::string string, uint8_t new_index) {
    auto promise = std::make_shared< std::promise< std::optional< std::string > > >();
    Future_optional_string result_future = promise->get_future();
    _map.write< wait_readers_to_leave >(
      [promise = std::move(promise), string = std::move(string), new_index] (Multi_index_map& sources) mutable -> void {
        const auto old_string = sources.swap_string_into_new_uint(string, new_index);
        promise->set_value(old_string ? std::make_optional(old_string->get()) : std::nullopt);
      }
    );

    return result_future;
  }

 private:
  core::rcu::MRSW_cell< Multi_index_map > _map;
};


} // namespace core::rcu