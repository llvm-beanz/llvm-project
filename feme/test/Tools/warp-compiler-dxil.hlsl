// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// REQUIRES: feme-warp-compiler, system-dxc-dxil
// RUN: %dxil_dxc -T cs_6_0 -E main -Fo %t.dxil %s
// RUN: feme-warp-compiler-test %t.dxil --dxil | FileCheck %s
// RUN: feme-warp-compiler-test %t.dxil --dxil-ddi | FileCheck %s --check-prefix=DDI
// RUN: feme-warp-compiler-test %t.dxil --dxil-bad-length 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: feme-warp-compiler-test %t.dxil --dxil-bad-offset 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: feme-warp-compiler-test %t.dxil --dxil-bad-size 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: feme-warp-compiler-test %t.dxil --dxil-bad-kind 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: %dxil_dxc -T cs_6_0 -E main -DBUFFER=1 -Fo %t.buffer.dxil %s
// RUN: feme-warp-compiler-test %t.buffer.dxil --dxil-dispatch | FileCheck %s --check-prefix=GROUPED
// RUN: %dxil_dxc -T cs_6_6 -E main -DBUFFER=1 -DWAVE_SIZE=4 -Fo %t.wave4.dxil %s
// RUN: feme-warp-compiler-test %t.wave4.dxil --dxil-dispatch | FileCheck %s --check-prefix=GROUPED
// RUN: %dxil_dxc -T cs_6_6 -E main -DBUFFER=1 -DWAVE_SIZE=8 -Fo %t.wave8.dxil %s
// RUN: feme-warp-compiler-test %t.wave8.dxil --dxil-wave-reject 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: %dxil_dxc -T cs_6_0 -E main -DBUFFER=1 -DCBV=1 -DROOT_REGISTER=u3 -Fo %t.cbv.dxil %s
// RUN: feme-warp-compiler-test %t.cbv.dxil --dxil-cbv-dispatch | FileCheck %s --check-prefix=GROUPED
// RUN: %dxil_dxc -T cs_6_0 -E main -DBUFFER=1 -DCBV=1 -DSINGLE_CBV=1 -DROOT_REGISTER=u3 -Fo %t.single.cbv.dxil %s
// RUN: feme-warp-compiler-test %t.single.cbv.dxil --dxil-single-cbv-dispatch | FileCheck %s --check-prefix=GROUPED

#ifdef BUFFER
#ifndef ROOT_REGISTER
#define ROOT_REGISTER u0
#endif
#ifdef TYPED_VIEWS
#ifndef TYPED_OUTPUT_SCALAR
#define TYPED_OUTPUT_SCALAR TYPED_SCALAR
#endif
#ifdef DESCRIPTOR_ARRAYS
RWBuffer<TYPED_OUTPUT_SCALAR> output[2] : register(ROOT_REGISTER);
Buffer<TYPED_SCALAR> input[2] : register(t3);
#else
RWBuffer<TYPED_OUTPUT_SCALAR> output : register(ROOT_REGISTER);
Buffer<TYPED_SCALAR> input : register(t3);
#endif
#elif defined(STRUCTURED_VIEWS)
#ifdef DESCRIPTOR_ARRAYS
RWStructuredBuffer<uint> output[2] : register(ROOT_REGISTER);
#else
RWStructuredBuffer<uint> output : register(ROOT_REGISTER);
#endif
#ifdef DESCRIPTOR_ARRAYS
StructuredBuffer<uint2> input[2] : register(t3);
#else
StructuredBuffer<uint2> input : register(t3);
#endif
#else
#ifdef DESCRIPTOR_ARRAYS
RWByteAddressBuffer output[2] : register(ROOT_REGISTER);
#else
RWByteAddressBuffer output : register(ROOT_REGISTER);
#endif
#ifdef BUFFER_VIEWS
#ifdef DESCRIPTOR_ARRAYS
ByteAddressBuffer input[2] : register(t3);
#else
ByteAddressBuffer input : register(t3);
#endif
#endif
#endif
#ifdef CBV
#ifdef DESCRIPTOR_ARRAYS
struct ConstantsType { uint values[2]; };
ConstantBuffer<ConstantsType> A[2] : register(b3);
ConstantBuffer<ConstantsType> B[2] : register(b3, space7);
#else
#ifdef INLINE_CONSTANTS
cbuffer A : register(b3) { uint4 firstA; uint lastA; };
cbuffer B : register(b3, space7) { uint4 firstB; uint lastB; };
#else
cbuffer A : register(b3) { uint valuesA[2]; };
#ifndef SINGLE_CBV
cbuffer B : register(b3, space7) { uint valuesB[2]; };
#endif
#endif
#endif
#endif
#ifdef DESCRIPTOR_ARRAYS
#define OUTPUT output[NonUniformResourceIndex(index % 2)]
#else
#define OUTPUT output
#endif
groupshared uint sharedValues[30];
#ifdef WAVE_SIZE
[WaveSize(WAVE_SIZE)]
#endif
[numthreads(5, 3, 2)]
void main(uint3 id : SV_DispatchThreadID, uint3 local : SV_GroupThreadID) {
  uint index = id.x + 15 * (id.y + 6 * id.z);
  uint slot = local.x + 5 * (local.y + 3 * local.z);
  sharedValues[slot] = index;
  GroupMemoryBarrierWithGroupSync();
  uint peer = sharedValues[slot == 29 ? 0 : slot + 1];
  uint bias = 23;
#ifdef CBV
#ifdef DESCRIPTOR_ARRAYS
  bias = A[NonUniformResourceIndex(index % 2)].values[id.x % 2] +
         B[NonUniformResourceIndex(index % 2)].values[id.x % 2];
#else
#ifdef INLINE_CONSTANTS
  bias = id.x % 2 ? lastA + lastB : firstA.x + firstB.x;
#else
  bias = valuesA[id.x % 2];
#ifndef SINGLE_CBV
  bias += valuesB[id.x % 2];
#endif
#endif
#endif
#endif
#ifdef COUNTER_VIEWS
  if (index % 2)
    OUTPUT[index] = output[1].COUNTER_OPERATION();
  else
    OUTPUT[index] = output[0].COUNTER_OPERATION();
#elif defined(TYPED_VIEWS)
#ifdef TYPED_VECTOR
  uint value = uint(OUTPUT[index]) + peer + bias +
               uint(input[NonUniformResourceIndex(index % 2)][index].y);
  OUTPUT[index] = value;
#else
#ifdef DESCRIPTOR_ARRAYS
  OUTPUT[index] = uint(OUTPUT[index]) + peer + bias +
                  uint(input[NonUniformResourceIndex(index % 2)][index]);
#else
  OUTPUT[index] = uint(OUTPUT[index]) + peer + bias + uint(input[index]);
#endif
#endif
#elif defined(STRUCTURED_VIEWS)
#ifdef DESCRIPTOR_ARRAYS
  OUTPUT[index] = OUTPUT[index] + peer + bias +
                  input[NonUniformResourceIndex(index % 2)][index].y;
#else
  OUTPUT[index] = OUTPUT[index] + peer + bias + input[index].y;
#endif
#else
#ifdef BUFFER_VIEWS
#ifdef DESCRIPTOR_ARRAYS
  bias += input[NonUniformResourceIndex(index % 2)].Load(index * 4);
#else
  bias += input.Load(index * 4);
#endif
#endif
  OUTPUT.Store(index * 4, OUTPUT.Load(index * 4) + peer + bias);
#endif
}
#else
[numthreads(1, 1, 1)]
void main() {}
#endif

// CHECK: native stage=5 wave=4 artifact={{[1-9][0-9]*}}
// CHECK-NEXT: native stage=5 wave=4 artifact={{[1-9][0-9]*}}
// CHECK-NEXT: compiled and released twice
// DDI: compiled DDI ownership PASS
// GROUPED: native grouped dispatch and concurrent PSOs PASS
// BAD: rejected without object
