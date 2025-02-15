// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.3-library -std=hlsl202x -fsyntax-only %s -verify

struct Dog {
  Dog() {} // expected-error{{constructors and destructors are unsupported in HLSL before 202y}}
  Dog(Dog D) {} // expected-error{{constructors and destructors are unsupported in HLSL before 202y}}
  ~Dog() {} // expected-error{{constructors and destructors are unsupported in HLSL before 202y}}
};
