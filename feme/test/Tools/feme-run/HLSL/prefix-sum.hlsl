// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme-run --wave-size=4 --groups=1,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s

// End-to-end coverage for `WavePrefixSum`/`WavePrefixCountBits` over a
// divergent mask: real HLSL, compiled to a DXIL DXContainer by Clang,
// imported/raised/JIT-dispatched by `feme-run` (see loop.hlsl's own comment
// for the pipeline this exercises in full). This is `prefix-sum.hlsl` from
// roadmap step R4 (feme/docs/Roadmap.md's §2.3): "WavePrefixSum/
// WavePrefixCountBits over a divergent mask; exercises §1.3's flag-selected
// `WavePrefixOp` family" -- the family `feme::dxil::OpRaisingPass::
// raiseReduceOpCall` raises, and `feme::cpu::WaveCallKind::PrefixSum`/
// `lowerPrefixReduce` (WaveLowering.cpp) lower for the CPU target.
//
// Every even lane (`tid.x % 2 == 0`) contributes its own `tid.x`; every odd
// lane contributes 0. Over this 4-wide wave that's contributions
// `[0, 0, 2, 0]` and mask `[true, false, true, false]`, so the exclusive
// prefix sum is `[0, 0, 0, 2]` and the exclusive prefix popcount is
// `[0, 1, 1, 2]`; `Out[tid.x] = prefixSum * 10 + prefixCount` is therefore
// `[0, 1, 1, 22]`.

// CHECK: binding[0:0][0]: 0 1 1 22

//--- shader.hlsl
RWStructuredBuffer<uint> Out : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  bool mask = (tid.x % 2) == 0;
  uint contribution = mask ? tid.x : 0;
  uint prefixSum = WavePrefixSum(contribution);
  uint prefixCount = WavePrefixCountBits(mask);
  Out[tid.x] = prefixSum * 10 + prefixCount;
}

//--- heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 16
