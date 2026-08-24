#pragma once
#include <type_traits>

template<class T, bool = std::is_enum<T>::value>
struct safe_underlying_type : std::underlying_type<T> { };

template<class T>
struct safe_underlying_type<T, false>
{
   using type = T;
};

template<bool> struct is_true;
template<> struct is_true<false> : std::false_type { };
template<> struct is_true<true> : std::true_type { };
