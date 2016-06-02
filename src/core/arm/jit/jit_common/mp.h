// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <type_traits>

/// Internal implementation of this header.
namespace mp_impl {

template<class... Ls>
struct mp_append_impl;

template<> struct mp_append_impl<> {
    using type = mp_list<>;
};

template<template<class...> class L, class... Ts>
struct mp_append_impl<L<Ts...>> {
    using type = L<Ts...>;
};

template<template<class...> class L1, class... T1s, template<class...> class L2, class... T2s, class... Ls>
struct mp_append_impl<L1<T1...>, L2<T2...>, Ls...> {
    using type = mp_append<L1<T1..., T2...>, Ls...>;
};

template<template<class> class F1, class T>
struct mp_filter_impl;

template<template<class> class F1, template<class...> class L1, class... T1>
struct mp_filter_impl<F1, L1<T1...>> {
    using type = mp_append<typename std::conditional<F1<T1>::value, L1<T1>, L1<>>::type...>;
};

} // namespace mp_impl

/// Metavalue representing true or false.
template<bool v> using mp_bool = std::integral_constant<bool, v>;

/// A list of types.
template <typename... Ts>
struct mp_list {};

/**
 * Metafunction that concatenates lists together.
 * @tparam Ls A variable number of lists to concatenate.
 */
template<class... Ls>
using mp_append = typename mp_append_impl<Ls...>::type;

/**
 * Metafunction that filters a list based on a predicate
 * @tparam L The list to filter.
 * @tparam F The predicate, a metafunction. F<T>::value must be true or false.
 */
template<template<class> class F, class L>
using mp_filter = typename mp_filter_impl<F, L>::type;
