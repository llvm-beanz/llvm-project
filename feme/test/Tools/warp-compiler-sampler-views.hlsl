// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// REQUIRES: feme-warp-compiler, system-dxc-dxil
// RUN: %dxil_dxc -T cs_6_0 -E main -Fo %t.dxil %s
// RUN: feme --wave-size=4 --target=%feme_host_triple %t.dxil -o %t.o
// RUN: %dxil_dxc -T cs_6_0 -E main -DSTATIC_SAMPLERS=1 -Fo %t.static.dxil %s
// RUN: feme --wave-size=4 --target=%feme_host_triple %t.static.dxil -o %t.static.o
// The standalone owned/reference D3D12 sampler-view gates execute this fixture.

Texture2DArray<float4> input[2] : register(t3, space7);
#ifdef STATIC_SAMPLERS
SamplerState sampling0 : register(s3, space7);
SamplerState sampling1 : register(s4, space7);
#else
SamplerState sampling[2] : register(s3, space7);
#endif
RWByteAddressBuffer output : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint index = NonUniformResourceIndex(id.x & 1);
  uint width, height, layers, mips;
  input[1].GetDimensions(0, width, height, layers, mips);
  float3 coordinate = float3((id.x + 0.25) / 4.0, 0.25, id.x >> 1);
  float4 value;
#ifdef STATIC_SAMPLERS
  if (index) {
    value = input[index].SampleLevel(sampling1, coordinate, 0);
    value += input[index].SampleLevel(
        sampling1, float3(-0.5, 0.25, id.x >> 1), 0);
  } else {
    value = input[index].SampleLevel(sampling0, coordinate, 0);
    value += input[index].SampleLevel(
        sampling0, float3(-0.5, 0.25, id.x >> 1), 0);
  }
#else
  value = input[index].SampleLevel(sampling[index], coordinate, 0);
  value += input[index].SampleLevel(
      sampling[index], float3(-0.5, 0.25, id.x >> 1), 0);
#endif
  output.Store4(4 + id.x * 32, uint4(value));
  output.Store4(20 + id.x * 32, uint4(width, height, layers, mips));
}
