// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -x hlsl -fsyntax-only -hlsl-entry main -verify %s

[numthreads(1,1, 1)]
void main(int GI) { } // expected-error{{entry function parameter 'GI' missing annotation}}
