// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme-run --dxil-bind-register-resources --wave-size=4 --groups=1,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s

// End-to-end coverage for wave operations: real HLSL, compiled to a DXIL
// DXContainer by Clang, imported/raised/JIT-dispatched by `feme-run` (see
// loop.hlsl's own comment for the pipeline this exercises in full).
// `WaveGetLaneCount`/`WaveActiveAnyTrue`/`WaveReadLaneAt`/
// `WaveActiveCountBits` are exactly the wave intrinsics roadmap milestone 8
// (`feme::cpu::WaveLoweringPass`) lowers -- see its own deviation note in
// feme/docs/FeMeCPUDesign.md's Status section for why those five and not
// others. Every lane computes the same value here (each intrinsic's result
// is uniform across the wave by construction), so a single
// wave-size-4-wide dispatch's four lanes should all agree.

// CHECK: heap[0]: 4104 4104 4104 4104

//--- shader.hlsl
RWStructuredBuffer<uint> Out : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint lanes = WaveGetLaneCount();
  bool anyOdd = WaveActiveAnyTrue((tid.x % 2) == 1);
  uint firstLaneVal = WaveReadLaneAt(tid.x, 0);
  uint cnt = WaveActiveCountBits(true);
  Out[tid.x] = lanes * 1000 + (anyOdd ? 100u : 0u) + firstLaneVal * 10u + cnt;
}

//--- heap.yaml
resource-heap:
  - index: 0
    size: 16
