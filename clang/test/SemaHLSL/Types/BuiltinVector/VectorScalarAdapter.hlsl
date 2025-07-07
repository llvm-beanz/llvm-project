// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.6-library -verify %s

// expected-no-diagnostics

template <typename T, typename U>
struct is_same {
  static const bool value = false;
};

template <typename T>
struct is_same<T, T> {
  static const bool value = true;
};

_Static_assert(is_same<vector_scalar_adapter<float,1>::Type, float>::value, "Woo!");
