//===----- detail.h - HLSL definitions for intrinsics ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _HLSL_HLSL_DETAILS_H_
#define _HLSL_HLSL_DETAILS_H_

namespace hlsl {

namespace __detail {

template <bool B, typename T> struct enable_if {};

template <typename T> struct enable_if<true, T> {
  using Type = T;
};

template <bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::Type;

template <typename U, typename T, int N>
constexpr enable_if_t<sizeof(U) == sizeof(T), vector<U, N>>
bit_cast(vector<T, N> V) {
  return __builtin_bit_cast(vector<U, N>, V);
}

template <typename U, typename T>
constexpr enable_if_t<sizeof(U) == sizeof(T), U> bit_cast(T F) {
  return __builtin_bit_cast(U, F);
}

template<typename T>
struct is_vector_type {
  constexpr static bool value = false;
};

template <typename T, unsigned N>
struct is_vector_type<T __attribute__((ext_vector_type(N)))> {
  constexpr static bool value = true;
};

template <typename T>
struct vector_type_info;

template <typename T, unsigned N>
struct vector_type_info<T __attribute__((ext_vector_type(N)))> {
  using Type = T;
  constexpr static unsigned Size = N;
};

template <typename T>
concept is_line_vector = sizeof(T) <= 16 && is_vector_type<T>::value
                         && vector_type_info<T>::Size <= 4;

} // namespace __detail
} // namespace hlsl
#endif //_HLSL_HLSL_DETAILS_H_
