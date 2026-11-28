// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// REQUIRES: feme-warp-compiler, system-dxc-dxil
// RUN: %dxil_dxc -T cs_6_0 -E main -Fo %t.dxil %s
// RUN: feme --wave-size=4 --target=%feme_host_triple %t.dxil -o %t.o
// The standalone owned/reference D3D12 texture-view gates execute this fixture.

Texture2DArray<uint4> input[2] : register(t3, space7);
RWByteAddressBuffer output : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint index = NonUniformResourceIndex(id.x & 1);
  uint width, height, layers, mips;
  input[1].GetDimensions(0, width, height, layers, mips);
  uint4 value = input[index].Load(int4(id.x, 0, id.x >> 1, 0));
  value += input[index].Load(int4(99, 0, 0, 0));
  output.Store4(4 + id.x * 32, value);
  output.Store4(20 + id.x * 32, uint4(width, height, layers, mips));
}
