#pragma once

#include "utils/general.hpp"
#include "utils/meta.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <concepts>
#include <functional>
#include <optional>
#include <stdexcept>
#include <thread>

#include <fmt/format.h>


namespace core::rcu {

namespace details {

// A reader type provides 'static T read(const T&)'.
template< class Reader, class T >
concept Has_static_read = requires(const T& value) {
  { Reader::read(value) } -> std::same_as< T >;
};

} // namespace details

// One Reader One Writer

// User can get two interfaces: Reading_interface (const or not) and Writing_interface
// Interfaces can be used from different threads at the same time.
// You cannot use the _same_ interface from two threads at the same time.

// "Reader" DOESN'T MEAN ACCESS WITHOUT MODIFICATION. It's just other kind of access.
// Actually "read" means "read or swap", so technically it's also "write".
// But user can restrict access kind (for example, access without modification) by
// choosing corresponding interface (const Reading_interface == without modification).
// There are 3 variants of 2 (!) interfaces (!): Reading_interface, const Reading_interface, Writing_interface

// const Reading_interface - only access without modification
// Reading_interface - read and swap
// Writing_interface - only write (without access to current value)

template <class T, class Read = void>
  requires
    (std::is_copy_assignable_v<T> || std::is_move_assignable_v<T>)
    && std::default_initializable<T>
class SRSW_cell {
  static_assert(
    std::is_same_v< Read, void >
    || details::Has_static_read< Read, T >,
    "If ReadFunction type not void, it must provide 'static T read(const T&)'"
  );

  static constexpr unsigned char _cells_number = 3;
  static constexpr unsigned char _max_tries = 100;

  template <class Flag_type>
  class Flag_guard {
   public:
    Flag_guard(std::atomic<Flag_type>& flag, Flag_type value_to_set_now, Flag_type value_to_set_on_exit)
      : _flag(flag), _value_to_set_on_exit(value_to_set_on_exit)
    {
      _flag.store(value_to_set_now);
    }

    Flag_guard(std::atomic<Flag_type>& flag, Flag_type value_to_set_on_exit)
      : _flag(flag), _value_to_set_on_exit(value_to_set_on_exit)
    {}

    /// CAS variant
    Flag_guard(
      std::atomic<Flag_type>& flag,
      Flag_type expected_value,
      Flag_type value_to_set_now,
      Flag_type value_to_set_on_exit,
      std::string_view throw_message_on_cas_fail
    )
      : _flag(flag)
      , _value_to_set_on_exit(value_to_set_on_exit)
    {
      if (!_flag.compare_exchange_strong(expected_value, value_to_set_now)) {
        throw std::runtime_error(throw_message_on_cas_fail.data());
      }
    }

    ~Flag_guard() {
      _flag.store(_value_to_set_on_exit);
    }

    Flag_guard(const Flag_guard&) = delete;
    Flag_guard(Flag_guard&&) = delete;
    Flag_guard& operator= (const Flag_guard&) = delete;
    Flag_guard& operator= (Flag_guard&&) = delete;

   private:
    std::atomic<Flag_type>& _flag;
    Flag_type _value_to_set_on_exit;
  };

  class Writing_interface {
    friend SRSW_cell;
   public:
    Writing_interface() = delete;
    Writing_interface(const Writing_interface&) = delete;
    Writing_interface(Writing_interface&&) = delete;

    ~Writing_interface() {
      _cell.return_writing_interface();
    }

    Writing_interface& operator = (const Writing_interface&) = delete;
    Writing_interface& operator = (Writing_interface&&) = delete;

    void write(const T& value) requires std::is_copy_assignable_v<T> {
      return _cell.write(value);
    }

    void write(T&& value) requires std::is_move_assignable_v<T>  {
      return _cell.write(std::move(value));
    }

   private:
    Writing_interface(SRSW_cell& cell) : _cell(cell) {}

    SRSW_cell& _cell;
  };

  class Reading_interface {
    friend SRSW_cell;
   public:
    Reading_interface() = delete;
    Reading_interface(const Reading_interface&) = delete;
    Reading_interface(Reading_interface&&) = delete;
    Reading_interface(const Reading_interface&&) = delete;

    ~Reading_interface() {
      _cell.return_reading_interface();
    }

    Reading_interface& operator = (const Reading_interface&) = delete;
    Reading_interface& operator = (Reading_interface&&) = delete;

    T read() const {
      return _cell.read();
    }

    void swap(T& swappable) {
      return _cell.swap(swappable);
    }

    T read_and_flush() requires std::is_default_constructible_v<T> {
      return _cell.read_and_flush();
    }

   private:
    Reading_interface(SRSW_cell& cell) : _cell(cell) {}
    Reading_interface(const SRSW_cell& cell) : _cell(const_cast<SRSW_cell&>(cell)) {}

    SRSW_cell& _cell;
  };

 public:
  enum class Access_kind {
    READ,
    SWAP,
    WRITE
  };

  template <Access_kind access_kind, class Dummy = void>
  static constexpr auto get_interface_type_sp() {
    if constexpr (access_kind == Access_kind::READ) {
      return std::shared_ptr<const Reading_interface>{};
    }
    else if constexpr (access_kind == Access_kind::SWAP) {
      return std::shared_ptr<Reading_interface>{};
    }
    else if constexpr (access_kind == Access_kind::WRITE) {
      return std::shared_ptr<Writing_interface>{};
    }
    else {
      static_assert(utils::meta::ALWAYS_FALSE<Dummy>, "Unknown access kind");
    }
  }

  template <Access_kind access_kind>
  using Get_interface_type = typename decltype(get_interface_type_sp<access_kind>())::element_type;

  // predefined for friend declaration
  template <Access_kind access_kind>
  class Interface_wrapper;

  template <Access_kind access_kind>
  class Interface : public std::shared_ptr<Get_interface_type<access_kind> > {
    friend class Interface_wrapper<access_kind>;
    friend SRSW_cell;
   public:
    using Interface_type = Get_interface_type<access_kind>;

    Interface() = delete;
    Interface(const Interface&) = delete;
    Interface(Interface&&) = delete;
    Interface(const Interface&& copy) noexcept : std::shared_ptr<Interface_type>(copy.get_sp()) {};

    Interface& operator =(const Interface&) = delete;
    Interface& operator =(Interface&&) = delete;

    using std::shared_ptr<Interface_type>::operator->;
    using std::shared_ptr<Interface_type>::operator*;

   private:
    explicit Interface(Interface_type* const&& pointer) : std::shared_ptr<Interface_type>(pointer) {};

    const std::shared_ptr<Interface_type>& get_sp() const {
      return (*this);
    };
  };

  template <Access_kind access_kind>
  class Interface_wrapper : public std::unique_ptr<Interface<access_kind> > {
   public:
    using std::unique_ptr<Interface<access_kind> >::unique_ptr;
    explicit Interface_wrapper(typename Interface<access_kind>::Interface_type* const&& pointer)
      : std::unique_ptr<Interface<access_kind> >(new Interface<access_kind>(std::move(pointer))) {};

    explicit Interface_wrapper(Interface<access_kind> const&& interface)
      : std::unique_ptr<Interface<access_kind> >(new Interface<access_kind>(std::move(interface))) {};

    typename Interface<access_kind>::Interface_type* operator->() const noexcept {
      return &**this;
    }

    typename Interface<access_kind>::Interface_type& operator*() const noexcept {
      return *(this->std::unique_ptr<Interface<access_kind> >::operator*());
    }
  };

  using Interface_wrapper_read = Interface_wrapper<Access_kind::READ>;
  using Interface_wrapper_swap = Interface_wrapper<Access_kind::SWAP>;
  using Interface_wrapper_write = Interface_wrapper<Access_kind::WRITE>;

  using Read_function = std::optional< std::function< T (const T&)> >;

 public:
  explicit SRSW_cell(const T& default_value) {
    _cells.fill(default_value);
  }

  explicit SRSW_cell()  {};
  SRSW_cell(const SRSW_cell&) = delete;
  SRSW_cell(SRSW_cell&&) = delete;

  template <Access_kind access_kind>
  Interface_wrapper<access_kind> get_interface_wrapper() noexcept {
    static_assert(access_kind != Access_kind::READ, "Use 'get_interface_wrapper_const' for READ access");
#define GET_INTERFACE_WRAPPER_INNER(kind, success, fail) \
    auto& flag =                                         \
      ((kind) == Access_kind::WRITE)                     \
      ? _is_writing_interface_currently_used             \
      : _is_reading_interface_currently_used;            \
                                                         \
    if (flag.compare_exchange_expect_false()) {          \
      success                                            \
    }                                                    \
    else {                                               \
      fail                                               \
    }

    GET_INTERFACE_WRAPPER_INNER(
      access_kind,
      return Interface_wrapper< access_kind >(new Get_interface_type< access_kind >(*this));,
      return {};
    )
  }

  Interface_wrapper<Access_kind::READ> get_interface_wrapper_const() const noexcept {
    GET_INTERFACE_WRAPPER_INNER(
      Access_kind::READ,
      return Interface_wrapper< Access_kind::READ >(new Get_interface_type< Access_kind::READ >(*this));,
      return {};
    )
  }

  template <Access_kind access_kind>
  Interface<access_kind> get_interface() {
    static_assert(access_kind != Access_kind::READ, "Use 'get_interface_const' for READ access");
    GET_INTERFACE_WRAPPER_INNER(
      access_kind,
      return Interface< access_kind >(new Get_interface_type< access_kind >(*this));,
      throw std::runtime_error(
        fmt::format("Interface (kind {}) of SRSW_cell already used", utils::to_underlying(access_kind))
      );
    )
  }

  Interface<Access_kind::READ> get_interface_const() const {
    GET_INTERFACE_WRAPPER_INNER(
      Access_kind::READ,
      return Interface< Access_kind::READ >(new Get_interface_type< Access_kind::READ >(*this));,
      throw std::runtime_error(
        fmt::format("Interface (kind READ) of SRSW_cell already used")
      );
    )
  }

 private:
  void write(const T& value) requires std::is_copy_assignable_v<T> {
#define SRSW_CELL_WRITE_INNER(...) \
    auto cell_index = (_last_written_cell.load() + 1) % _cells_number; \
    if (cell_index == _current_reading_cell.load()) {                  \
      cell_index = (cell_index + 1) % _cells_number;                   \
    }                                                                  \
                                                                       \
    __VA_ARGS__                                                        \
                                                                       \
    _last_written_cell.store(cell_index)

    SRSW_CELL_WRITE_INNER(
      _cells[cell_index] = value;
    );
  }

  void write(T&& value) requires std::is_move_assignable_v<T> {
    SRSW_CELL_WRITE_INNER(
      _cells[cell_index] = std::move(value);
    );
  }

  T read() const {
#define SRSW_CELL_READ_ACCESS_INNER(...)                                                                                  \
    unsigned char cell_index = _last_written_cell.load();                                                                 \
    for (size_t i = 0; i < _max_tries; ++i) {                                                                             \
      Flag_guard reading_cell_guard(_current_reading_cell, cell_index, _cells_number);                                    \
                                                                                                                          \
      /* cell index check for case when "writing end" and "writing start" runs between # _last_written_cell.load() */     \
      /* and # _current_reading_cell.store(cell_index). In this case writer can't decide which cell to write to */        \
      /* because _current_reading_cell doesn't set yet and cell_index represent previous (invalid) value */               \
      /* For example: before _current_reading_cell.store(cell_index): cell_index (read) == 0, _last_written_cell == 2 */  \
      /* write ends, then new write call will edit cell[0], and after that _current_reading_cell.store(0); will called */ \
      const auto current_last_written_cell = _last_written_cell.load();                                                   \
      /* it means write() at this moment can (but not necessarily) operate over same cell */                              \
      if (cell_index == (current_last_written_cell + 1) % _cells_number) {                                                \
        cell_index = current_last_written_cell;                                                                           \
        continue;                                                                                                         \
      }                                                                                                                   \
                                                                                                                          \
      __VA_ARGS__                                                                                                         \
    }

// define SRSW_CELL_READ_ACCESS_INNER end

    SRSW_CELL_READ_ACCESS_INNER(
      if constexpr (!std::is_same_v< Read, void >) {
        return Read::read(_cells[cell_index]);
      }
      else {
        return _cells[cell_index];
      }
    )

    throw std::runtime_error("SRSW_cell: Failed to read");
  }

  void swap(T& swappable) {
    SRSW_CELL_READ_ACCESS_INNER(
      std::swap(_cells[cell_index], swappable);
      return;
    )

    throw std::runtime_error("SRSW_cell: Failed to swap");
  }

  T read_and_flush() requires std::is_default_constructible_v<T> {
    auto result = T{};
    swap(result);
    return result;
  }

 private:
  void return_reading_interface() const {
    _is_reading_interface_currently_used.store(false);
  }

  void return_writing_interface() const {
    _is_writing_interface_currently_used.store(false);
  }

 private:
  std::array<T, _cells_number> _cells;

  mutable std::atomic<unsigned char> _last_written_cell = 0;
  mutable std::atomic<unsigned char> _current_reading_cell = _cells_number;

  // not for synchronization purpose, but for readers/writers limiting
  mutable core::utils::Atomic_flag _is_writing_interface_currently_used = false;
  mutable core::utils::Atomic_flag _is_reading_interface_currently_used = false;
};

}