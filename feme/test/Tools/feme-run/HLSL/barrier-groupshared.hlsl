// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme-run --dxil-bind-register-resources --wave-size=4 --groups=4,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s

// End-to-end coverage for barriers and groupshared memory: real HLSL,
// compiled to a DXIL DXContainer by Clang, imported/raised/JIT-dispatched
// by `feme-run` (see loop.hlsl's own comment for the pipeline this
// exercises in full). Every lane of a group publishes the same
// (group-uniform) value into `Shared` before
// `GroupMemoryBarrierWithGroupSync()`; roadmap milestone 9
// (`feme::cpu::EntryWrapperPass`'s barrier region splitting,
// `feme::cpu::rewriteGroupSharedGlobals`) is what makes that write visible
// to every wave of the group by the time the barrier returns. Four groups
// dispatch here (`--groups=4,1,1`), each writing a distinct value derived
// from its own `SV_GroupID` into a distinct heap slot, so a wrong answer in
// any slot means the barrier or the groupshared allocation itself -- not
// just per-lane arithmetic -- got a group's execution order wrong.
//
// This only reads/writes `Shared` at the *same* index from every lane
// (`Shared[0]`, never a per-lane index like `Shared[tid.x]`) and never
// through an atomic: `feme::cpu::rewriteGroupSharedGlobals`'s own comment
// (GroupShared.cpp) notes that only a uniform load/store/getelementptr is
// canonicalized as of this milestone; a divergent (per-lane) groupshared
// index or a groupshared atomic both remain future work. This also reads
// `SV_GroupID`, not `SV_DispatchThreadID`, after the barrier:
// `splitAtGroupSyncBarriers`'s own liveness check (EntryWrapper.cpp) allows
// a WaveBody parameter like the group ID to survive a barrier, but not yet
// an arbitrary per-lane SSA value computed before it (see its own comment
// for the still-open context-spilling gap that narrows).

// CHECK: heap[0]: 100 101 102 103

//--- shader.hlsl
RWStructuredBuffer<uint> Out : register(u0);
groupshared uint Shared[1];

[numthreads(4, 1, 1)]
void main(uint3 gid : SV_GroupID) {
  Shared[0] = 100 + gid.x;
  GroupMemoryBarrierWithGroupSync();
  Out[gid.x] = Shared[0];
}

//--- heap.yaml
resource-heap:
  - index: 0
    size: 16
