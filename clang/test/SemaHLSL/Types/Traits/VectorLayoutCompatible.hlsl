// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.6-library -finclude-default-header -verify %s

// expected-no-diagnostics

struct Empty {};

static_assert(!__builtin_hlsl_is_vector_layout_compatible(Empty), "Empty");

struct OneFloat {
  float A;
};

static_assert(__builtin_hlsl_is_vector_layout_compatible(OneFloat), "OneFloat");

struct TwoFloat {
  float A;
  float B;
};

static_assert(__builtin_hlsl_is_vector_layout_compatible(TwoFloat), "TwoFloat");

struct TwoFloat2 {
  float A[2];
};

static_assert(__builtin_hlsl_is_vector_layout_compatible(TwoFloat2), "TwoFloat2");

struct TwoFloat3 {
  float2 A;
};

static_assert(__builtin_hlsl_is_vector_layout_compatible(TwoFloat3), "TwoFloat3");

struct TwoFloat4 {
  TwoFloat A;
};

static_assert(__builtin_hlsl_is_vector_layout_compatible(TwoFloat4), "TwoFloat4");

struct TwoFloat5 {
  OneFloat A;
  OneFloat B;
};

static_assert(__builtin_hlsl_is_vector_layout_compatible(TwoFloat5), "TwoFloat5");

struct FiveFloats {
  float A[5];
};

static_assert(!__builtin_hlsl_is_vector_layout_compatible(FiveFloats), "FiveFloats");

struct FloatInt {
  float A;
  int B;
};

static_assert(!__builtin_hlsl_is_vector_layout_compatible(FloatInt), "FloatInt");

struct UintInt {
  uint A;
  int B;
};

static_assert(!__builtin_hlsl_is_vector_layout_compatible(UintInt), "UintInt");


// Some array tests!

static_assert(__builtin_hlsl_is_vector_layout_compatible(int[1]), "int[1]");
static_assert(__builtin_hlsl_is_vector_layout_compatible(int[2]), "int[2]");
static_assert(__builtin_hlsl_is_vector_layout_compatible(int[3]), "int[3]");
static_assert(__builtin_hlsl_is_vector_layout_compatible(int[4]), "int[4]");
static_assert(!__builtin_hlsl_is_vector_layout_compatible(int[5]), "int[5]");

static_assert(__builtin_hlsl_is_vector_layout_compatible(TwoFloat2[2]), "TwoFloat2[2]");
static_assert(!__builtin_hlsl_is_vector_layout_compatible(TwoFloat2[3]), "TwoFloat2[3]");
