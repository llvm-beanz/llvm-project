// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme-run --wave-size=4 --groups=3,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s

// End-to-end coverage for roadmap step R5 (feme/docs/Roadmap.md's §2.3):
// several groups, each publishing through *two* barrier-separated
// groupshared slots, asserting that one group's groupshared state never
// leaks into another's. Real HLSL compiled to a DXIL DXContainer by Clang
// and imported/raised/JIT-dispatched by `feme-run` (see loop.hlsl's own
// comment for the pipeline this exercises in full).
//
// Every lane of a group publishes the same (group-uniform,
// `SV_GroupID`-derived) value into `Shared[0]`, then, after a barrier,
// every lane republishes a value derived from *that* into `Shared[1]`,
// then, after a second barrier, every lane combines both slots into this
// group's own answer. `feme::cpu::EntryWrapperPass` splits this into three
// regions (two barriers), each wrapped in its own wave loop by the group's
// wrapper call; `feme::cpu::EntryWrapperPass`'s groupshared allocation
// (see "Groupshared memory" in "Phase 6: Group Execution and Barriers")
// gives each of the three dispatched groups' wrapper calls its own,
// independently-allocated buffer, so a wrong answer in any group's slot
// means either the barrier ordering or the groupshared allocation itself
// let a group observe another's memory -- not just per-lane arithmetic.
// `--heap`'s buffer holds 4 `uint`s but only 3 groups dispatch
// (`--groups=3,1,1`); the 4th, never written, stays `0`, itself evidence
// that no group wrote outside its own `Out[gid.x]` slot.

// CHECK: binding[0:0][0]: 11 22 33 0

//--- shader.hlsl
RWStructuredBuffer<uint> Out : register(u0);
groupshared uint Shared[2];

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_GroupThreadID, uint3 gid : SV_GroupID) {
  Shared[0] = gid.x + 1;
  GroupMemoryBarrierWithGroupSync();
  Shared[1] = Shared[0] * 10;
  GroupMemoryBarrierWithGroupSync();
  Out[gid.x] = Shared[0] + Shared[1];
}

//--- heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 16
