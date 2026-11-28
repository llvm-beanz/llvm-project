// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// REQUIRES: feme-warp-compiler, system-dxc-dxil
// RUN: %dxil_dxc -T cs_6_0 -E main -Fo %t.dxil %s
// RUN: feme --wave-size=4 --target=%feme_host_triple %t.dxil -o %t.o
// RUN: %dxil_dxc -T cs_6_0 -E main -DVOLUME=1 -Fo %t.volume.dxil %s
// RUN: feme --wave-size=4 --target=%feme_host_triple %t.volume.dxil -o %t.volume.o
// Standalone owned/reference gates check selected writes and every subresource.

#ifdef VOLUME
RWTexture3D<uint4> input[2] : register(u3, space7);
#else
RWTexture2DArray<uint4> input[2] : register(u3, space7);
#endif
RWByteAddressBuffer output : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint index = NonUniformResourceIndex(id.x & 1);
  uint width, height, layers;
  input[1].GetDimensions(width, height, layers);
  uint3 coordinate = uint3(id.x, 0, id.x >> 1);
  input[index][coordinate] =
      uint4(256 + id.x, 65536 + id.x, 0xffffffff - id.x, 17 + id.x);
  input[index][uint3(99, 0, 0)] = uint4(777, 777, 777, 777);
  uint4 value = input[index][coordinate] + input[index][uint3(99, 0, 0)];
#ifdef VOLUME
  input[index][uint3(id.x, 0, 2)] = uint4(777, 777, 777, 777);
  value += input[index][uint3(id.x, 0, 2)];
#endif
  output.Store4(4 + id.x * 32, value);
  output.Store4(20 + id.x * 32, uint4(width, height, layers, 1));
}
