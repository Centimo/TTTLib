#pragma once

#include <algorithm>
#include <functional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>


namespace core::utils {

template< class T>
concept Tuple_like = requires { typename std::tuple_size< std::remove_cvref_t< T>>::type; };

namespace details {

// Makes a std::bind_back result usable on the right-hand side of |. Kept out of the public interface on
// purpose: [range.adaptor.object]/5 leaves the behaviour undefined when the wrapped callable is itself a
// range adaptor closure, and in libstdc++ such a wrapper silently loses its operator| instead of failing
// to compile. Only this file decides what gets wrapped.
template< class Function>
struct Closure : Function, std::ranges::range_adaptor_closure< Closure< Function>> {
  constexpr explicit Closure(Function function) : Function(std::move(function)) {}
};

template< class Function, class Tuple, std::size_t... INDEXES>
constexpr bool is_applicable(std::index_sequence< INDEXES...>) {
  return std::invocable< Function, decltype(std::get< INDEXES>(std::declval< Tuple>()))...>;
}

// Whether the function can be called with the components of the tuple. std::apply itself is
// unconstrained, so without this the arity and type check would be deferred to its body: std::invocable
// would answer true, views::transform would accept the adaptor, and the mismatch would surface as an
// error inside std::apply rather than as a rejected pipe.
template< class Function, class Tuple>
concept Applicable =
  Tuple_like< Tuple>
  && is_applicable< Function, Tuple>(
       std::make_index_sequence< std::tuple_size_v< std::remove_cvref_t< Tuple>>>{});

// Calls the function with the components of a tuple element instead of the element itself. The result
// type follows the function, references included — the same rule views::transform uses.
template< class Function>
class Unpack {
 public:
  constexpr explicit Unpack(Function function) : _function(std::move(function)) {}

  template< class Tuple>
    requires Applicable< const Function&, Tuple>
  [[nodiscard]] constexpr decltype(auto) operator () (Tuple&& tuple) const {
    return std::apply(_function, std::forward< Tuple>(tuple));
  }

  // Mutable callables work in views::transform, so they work here too.
  template< class Tuple>
    requires Applicable< Function&, Tuple>
  [[nodiscard]] constexpr decltype(auto) operator () (Tuple&& tuple) {
    return std::apply(_function, std::forward< Tuple>(tuple));
  }

 private:
  [[no_unique_address]] Function _function;
};

// The conversion behind `cast`: a plain static_cast, so nothing it allows is filtered out here.
//
// A reference Target is rejected by both conversions below: converting into one would return a reference
// to the temporary built inside the call.
template< class Target>
  requires (!std::is_reference_v< Target>)
class Cast {
 public:
  template< class Source>
    requires requires (Source&& value) { static_cast< Target>(std::forward< Source>(value)); }
  [[nodiscard]] constexpr Target operator () (Source&& value) const {
    return static_cast< Target>(std::forward< Source>(value));
  }
};

// The conversion behind `as`: list-initialization, which is exactly a cast minus the narrowing. Signed
// to unsigned, wider to narrower and floating to integral all narrow and fail this constraint.
template< class Target>
  requires (!std::is_reference_v< Target>)
class Convert {
 public:
  template< class Source>
    requires requires (Source&& value) { Target{std::forward< Source>(value)}; }
  [[nodiscard]] constexpr Target operator () (Source&& value) const {
    return Target{std::forward< Source>(value)};
  }
};

} // namespace details

// Pipe form of std::views::zip: fixes every range but the leftmost one, which the pipe supplies.
// Note: range-v3 uses the name zip_with for what the standard calls zip_transform — this one only zips.
//
// Lifetime follows the usual view rules, with one addition: a range passed as an rvalue is moved into an
// owning_view and stays alive inside the resulting view, so a temporary container is safe to pass. An
// lvalue is captured as a ref_view and dangles if its source dies first, exactly like any other view. A
// closure bound to a temporary is move-only and therefore single-use; bind one to an lvalue to reuse it.
template< std::ranges::viewable_range... Ranges>
[[nodiscard]] constexpr auto zip_with(Ranges&&... ranges) {
  return details::Closure(
    std::bind_back(std::views::zip, std::views::all(std::forward< Ranges>(ranges))...));
}

// Two conversion steps of a chain, differing in what they refuse:
//
//   | as< std::int64_t>     value-preserving conversions only; narrowing does not compile
//   | cast< std::size_t>    whatever static_cast does, narrowing and sign changes included
//
// Reach for `as` by default and for `cast` where the loss is intended — the name is then the warning.
// cast also carries everything else static_cast can do, a pointer downcast among them, which is
// unchecked and undefined when the dynamic type is wrong.
template< class Target>
inline constexpr auto as = std::views::transform(details::Convert< Target>{});

template< class Target>
inline constexpr auto cast = std::views::transform(details::Cast< Target>{});

// Transform whose function takes the tuple components as separate arguments:
//   | unpack([](const auto& stride, const auto& size) { ... })
template< class Function>
[[nodiscard]] constexpr auto unpack(Function function) {
  return std::views::transform(details::Unpack< Function>(std::move(function)));
}

class Multiply {
 public:
  template< Tuple_like Tuple>
  [[nodiscard]] constexpr auto operator () (Tuple&& tuple) const {
    static_assert(
      std::tuple_size_v< std::remove_cvref_t< Tuple>> > 0, "Multiply: an empty tuple has no product");

    return std::apply(
      [](auto&&... components) { return (components * ...); }, std::forward< Tuple>(tuple));
  }
};

class Sum : public std::ranges::range_adaptor_closure< Sum> {
 public:
  // The accumulator is whatever + yields, so types subject to integral promotion (uint8_t and friends)
  // widen on their own; types that do not (uint32_t, size_t) wrap around, and widening them is a separate
  // step of the chain: | transform(widen) | sum.
  template< std::ranges::input_range Range>
  [[nodiscard]] constexpr auto operator () (Range&& range) const {
    // Deliberately a hard error rather than a constraint: a constraint would make the pipe fail with
    // 'no match for operator|' and a fully expanded zip_view type instead of naming the actual problem.
    // The trade-off is that the rejection cannot be probed with std::invocable.
    if constexpr (Tuple_like< std::ranges::range_value_t< Range>>) {
      static_assert(
        false, "Sum: elements are tuple-like, not scalars; reduce them first, e.g. | transform(mul)");
    }
    else {
      using Value = std::ranges::range_value_t< Range>;
      return std::ranges::fold_left(std::forward< Range>(range), Value{}, std::plus{});
    }
  }
};

inline constexpr Multiply mul;
inline constexpr Sum sum;

template<typename... Ranges>
auto combine(Ranges&&... rngs) {
  return std::views::zip(std::forward<Ranges>(rngs)...);
}

} // namespace core::utils
