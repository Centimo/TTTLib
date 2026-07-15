#include "core/utils/meta.hpp"

#include <gtest/gtest.h>

#include <tuple>
#include <type_traits>

namespace meta = core::utils::meta;

namespace {
template< int R > struct Tag { static constexpr int rank = R; };

// Distinct types that may be order-equivalent (compared only by Key, ignoring Id).
template< int Key, int Id > struct Keyed { static constexpr int key = Key; };
} // namespace

template< int A, int B >
struct core::utils::meta::Less< Tag< A >, Tag< B > > {
  static constexpr bool value = A < B;
};

template< int K1, int I1, int K2, int I2 >
struct core::utils::meta::Less< Keyed< K1, I1 >, Keyed< K2, I2 > > {
  static constexpr bool value = K1 < K2;
};

namespace {

// ---- Pack_element ----
static_assert(std::is_same_v< meta::Pack_element< 0, int, char, double >, int >);
static_assert(std::is_same_v< meta::Pack_element< 2, int, char, double >, double >);

// ---- Make_subtuple ----
static_assert(std::is_same_v<
  meta::Make_subtuple< std::tuple< Tag<0>, Tag<1>, Tag<2>, Tag<3> >, 1, 3 >::type,
  std::tuple< Tag<1>, Tag<2> > >);

// ---- Filter_types_list ----
template< class T > struct Is_even { static constexpr bool value = (T::rank % 2 == 0); };
static_assert(std::is_same_v<
  meta::Filter_types_list< Is_even, std::tuple< Tag<1>, Tag<2>, Tag<3>, Tag<4> > >::type,
  std::tuple< Tag<2>, Tag<4> > >);

// ---- Sort: unsorted input with a duplicate ----
using Unsorted = std::tuple< Tag<3>, Tag<1>, Tag<4>, Tag<1>, Tag<5>, Tag<2> >;
using Sorter   = meta::Sort< Unsorted >;
static_assert(std::is_same_v<
  Sorter::type,
  std::tuple< Tag<1>, Tag<1>, Tag<2>, Tag<3>, Tag<4>, Tag<5> > >);

TEST(MetaSort, SortsUnsortedInput) {
  EXPECT_TRUE((std::is_same_v<
    Sorter::type,
    std::tuple< Tag<1>, Tag<1>, Tag<2>, Tag<3>, Tag<4>, Tag<5> > >));
}

TEST(MetaSort, FindOverSortedResult) {
  // find must binary-search the SORTED result, not the raw input
  EXPECT_EQ(Sorter::find< Tag<2> >().value(), 2u);
  EXPECT_EQ(Sorter::find< Tag<3> >().value(), 3u);
  EXPECT_EQ(Sorter::find< Tag<4> >().value(), 4u);
  EXPECT_EQ(Sorter::find< Tag<5> >().value(), 5u);
  EXPECT_TRUE(Sorter::find< Tag<1> >().has_value());
  EXPECT_LE(Sorter::find< Tag<1> >().value(), 1u);   // one of the two duplicates
  EXPECT_FALSE(Sorter::find< Tag<9> >().has_value());
}

TEST(MetaSort, DuplicateDetection) {
  EXPECT_TRUE(Sorter::_is_contains_duplicates);
  EXPECT_FALSE((meta::Sort< std::tuple< Tag<3>, Tag<1>, Tag<2> > >::_is_contains_duplicates));
}

TEST(MetaSort, FindUsesOrderingEquivalenceNotIdentity) {
  using S = meta::Sort< std::tuple< Keyed<3,0>, Keyed<1,0>, Keyed<2,0> > >;
  // The probe Keyed<2,99> is a DIFFERENT type but order-equivalent to Keyed<2,0>.
  // With the default find() equality tied to Less_l, it must be found (std::is_same would miss it).
  EXPECT_TRUE((S::find< Keyed<2,99> >().has_value()));
  EXPECT_EQ((S::find< Keyed<2,99> >().value()), 1u);
  EXPECT_FALSE((S::find< Keyed<5,0> >().has_value()));
}

TEST(MetaSort, EdgeSizes) {
  using S0 = meta::Sort< std::tuple<> >;
  using S1 = meta::Sort< std::tuple< Tag<7> > >;

  EXPECT_TRUE((std::is_same_v< S0::type, std::tuple<> >));
  EXPECT_FALSE(S0::find< Tag<1> >().has_value());
  EXPECT_FALSE(S0::_is_contains_duplicates);

  EXPECT_TRUE((std::is_same_v< S1::type, std::tuple< Tag<7> > >));
  EXPECT_EQ(S1::find< Tag<7> >().value(), 0u);
  EXPECT_FALSE(S1::find< Tag<8> >().has_value());
  EXPECT_FALSE(S1::_is_contains_duplicates);
}

} // namespace
