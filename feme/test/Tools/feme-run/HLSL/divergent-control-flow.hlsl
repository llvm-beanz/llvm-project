// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme-run --dxil-bind-register-resources --wave-size=4 --groups=1,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s

// End-to-end coverage for divergent control flow: real HLSL, compiled to a
// DXIL DXContainer by Clang, imported/raised/JIT-dispatched by `feme-run`
// (see loop.hlsl's own comment for the pipeline this exercises in full).
// Each lane's branch condition (`tid.x % 2`) genuinely varies across the
// wave, so this exercises `feme::cpu::LinearizePass` (roadmap milestone 6:
// linearizing the divergent diamond) and `feme::cpu::SIMDizePass`'s
// `select`-based widening of the two arms' predicated results together.

// CHECK: heap[0]: 100 201 102 203

//--- shader.hlsl
RWStructuredBuffer<uint> Out : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint v;
  if (tid.x % 2 == 0) {
    v = 100 + tid.x;
  } else {
    v = 200 + tid.x;
  }
  Out[tid.x] = v;
}

//--- heap.yaml
resource-heap:
  - index: 0
    size: 16
