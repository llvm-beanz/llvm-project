// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// REQUIRES: feme-warp-compiler, system-dxc-dxil
// RUN: %dxil_dxc -T cs_6_0 -E main -Fo %t.dxil %s
// RUN: feme-warp-compiler-test %t.dxil --dxil-samplers | FileCheck %s
// RUN: %dxil_dxc -T cs_6_6 -E main -DNATIVE_IMAGE_HEAP=1 -Fo %t.image.dxil %s
// RUN: feme-warp-compiler-test %t.image.dxil --dxil-native-image-heap-reject | FileCheck %s
// RUN: %dxil_dxc -T cs_6_6 -E main -DNATIVE_SAMPLER_HEAP=1 -Fo %t.sampler.dxil %s
// RUN: feme-warp-compiler-test %t.sampler.dxil --dxil-native-image-heap-reject | FileCheck %s

Texture2D<float> input[2] : register(t3, space7);
SamplerState samplers[2] : register(s3, space7);
RWByteAddressBuffer output : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint index = NonUniformResourceIndex(id.x & 1);
#ifdef NATIVE_IMAGE_HEAP
  Texture2D<float> image = ResourceDescriptorHeap[0];
  float value = image.Load(int3(0, 0, 0));
#elif defined(NATIVE_SAMPLER_HEAP)
  SamplerState sampler = SamplerDescriptorHeap[0];
  float value = input[index].SampleLevel(sampler, float2(0.5, 0.5), 0);
#else
  float value = input[index].SampleLevel(samplers[index], float2(0.5, 0.5), 0);
#endif
  output.Store(id.x * 4, asuint(value));
#if !defined(NATIVE_IMAGE_HEAP) && !defined(NATIVE_SAMPLER_HEAP)
  output.Store(16 + id.x * 4,
               asuint(input[index].SampleLevel(samplers[index],
                                               float2(1.5, -0.5), 0)));
#endif
}

// CHECK: native borrowed image/sampler execution and concurrent PSOs PASS
