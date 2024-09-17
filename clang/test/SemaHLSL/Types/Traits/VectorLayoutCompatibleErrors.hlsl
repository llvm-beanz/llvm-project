// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.6-library -finclude-default-header -verify %s

struct Incomplete; // expected-note{{forward declaration of 'Incomplete'}}

static_assert(__builtin_hlsl_is_vector_layout_compatible(Incomplete), "Incomplete"); //expected-error {{incomplete type 'Incomplete' used in type trait expression}}


// expected-error@#VLA{{variable length arrays are not supported for the current target}}
// expected-error@#VLA{{variable length arrays are not supported in '__builtin_hlsl_is_vector_layout_compatible'}}
// expected-error@#VLA{{static assertion failed due to requirement '__builtin_hlsl_is_vector_layout_compatible(int[X])': VLA}}
// expected-warning@#VLA{{variable length arrays in C++ are a Clang extension}}
// expected-note@#VLA{{function parameter 'X' with unknown value cannot be used in a constant expression}}
void fn(int X) { // expected-note{{declared here}}
  static_assert(__builtin_hlsl_is_vector_layout_compatible(int[X]), "VLA"); // #VLA
}

static_assert(!__builtin_hlsl_is_vector_layout_compatible(int[]), "Incomplete Array"); // expected-error{{incomplete type 'int[]' used in type trait expression}}
