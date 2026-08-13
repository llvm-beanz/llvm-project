// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme-run --dxil-bind-register-resources --wave-size=4 --groups=4,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s

// End-to-end coverage combining every use case above in one shader: a loop
// (roadmap milestone 7) whose body branches on divergent, per-lane data
// (roadmap milestone 6), a wave op broadcasting a uniform view of that
// per-lane result (roadmap milestone 8), and a barrier plus groupshared
// memory (roadmap milestone 9) publishing the combined, group-uniform
// result once per group -- see loop.hlsl/divergent-control-flow.hlsl/
// wave-ops.hlsl/barrier-groupshared.hlsl's own comments for each piece in
// isolation.
//
// Every lane computes a divergent per-lane `sum` from a loop whose trip
// count depends on nothing divergent (so `feme::cpu::SIMDizePass` can widen
// it), but whose body takes one of two arms depending on the (divergent)
// parity of `tid.x`. `WaveReadLaneAt(sum, 0)` reads lane 0's value
// specifically: since numthreads is 4 and `--wave-size=4`, lane 0 of every
// group is the group's first thread (`tid.x = gid.x * 4`, always even), so
// `lane0Sum` is the same, uniform value (10) for every group regardless of
// which group's wave computes it, letting this test predict the whole
// dispatch's output without needing to special-case any one group.
// `combined` (a uniform value: a broadcast wave read plus a wave-wide
// constant) is what actually crosses the barrier through `Shared` -- see
// barrier-groupshared.hlsl's own comment for why only a group-uniform value
// (never a per-lane one) can, as of this milestone.

// CHECK: heap[0]: 14 15 16 17

//--- shader.hlsl
RWStructuredBuffer<uint> Out : register(u0);
groupshared uint Shared[1];

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID, uint3 gid : SV_GroupID) {
  uint sum = 0;
  for (uint i = 0; i < 4; i++) {
    sum += (tid.x % 2 == 0) ? (i + 1) : (2 * (i + 1));
  }
  uint lane0Sum = WaveReadLaneAt(sum, 0);
  uint lanes = WaveGetLaneCount();
  uint combined = lane0Sum + lanes;

  Shared[0] = combined + gid.x;
  GroupMemoryBarrierWithGroupSync();
  Out[gid.x] = Shared[0];
}

//--- heap.yaml
resource-heap:
  - index: 0
    size: 16
