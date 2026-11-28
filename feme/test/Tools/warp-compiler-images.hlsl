// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// REQUIRES: feme-warp-compiler, system-dxc-dxil
// RUN: %dxil_dxc -T cs_6_0 -E main -Fo %t.dxil %s
// RUN: feme-warp-compiler-test %t.dxil --dxil-images | FileCheck %s

Texture2DArray<uint4> input[2] : register(t3, space7);
RWTexture2DArray<uint4> storage[2] : register(u3, space7);
RWByteAddressBuffer output : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint index = NonUniformResourceIndex(id.x & 1);
  output.Store4(id.x * 32, input[index].Load(int4(id.x >> 1, 1, 1, 1)));
  output.Store4(id.x * 32 + 16, storage[index][uint3(id.x >> 1, 0, 1)]);
}

// CHECK: native borrowed image/sampler execution and concurrent PSOs PASS
