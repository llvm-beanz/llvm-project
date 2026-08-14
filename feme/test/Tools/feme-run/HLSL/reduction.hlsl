// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme-run --wave-size=4 --groups=1,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s

// End-to-end coverage for roadmap step R5 (feme/docs/Roadmap.md's §2.3):
// a `GroupMemoryBarrierWithGroupSync` inside a loop, real HLSL compiled to
// a DXIL DXContainer by Clang and imported/raised/JIT-dispatched by
// `feme-run` (see loop.hlsl's own comment for the pipeline this exercises
// in full). `feme::cpu::EntryWrapperPass`'s `matchLoopShape`/
// `buildWrapperForLoop` (see EntryWrapper.cpp's file comment) is what makes
// this legal: the loop's own induction variable (`stride`) is hoisted into
// the wrapper as an ordinary scalar loop, while each of the two barriers'
// three regions still runs through the usual per-wave wave loop, once per
// iteration.
//
// `contribution` is a genuinely divergent (per-lane) value computed fresh
// each iteration and used only *after* the first barrier -- exercising
// `spillValuesLiveAcrossBarriers`'s per-wave context spilling -- but never
// carried across iterations itself (only `stride` is a loop-carried phi;
// see `LoopShape`'s doc comment for why a divergent loop-carried phi is not
// yet supported). `WaveActiveSum` folds the per-lane contributions back
// into a single group-uniform value, which every lane then publishes to
// (and reads back from) `Shared` *unconditionally* -- an honest reduction
// tree indexed per-lane (`Shared[tid.x]`) remains blocked on §1.6's
// separate "Divergent groupshared access is diagnosed" row (a *masked*
// groupshared store -- one only some lanes execute -- is not yet
// canonicalized, only an unconditional, uniformly-addressed one is; see
// GroupShared.cpp's own comment), which is a different gap than this step
// closes. Wave-size-dependent (`WaveActiveSum` folds over exactly the
// wave's own width), so this stays at the tree's overwhelmingly common
// `W = 4` like combined.hlsl/wave-ops.hlsl do, rather than joining the
// wave-size sweep.

// CHECK: binding[0:0][0]: 20 20 20 20

//--- shader.hlsl
RWStructuredBuffer<uint> Out : register(u0);
groupshared uint Shared[1];

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_GroupThreadID) {
  for (uint stride = 2; stride > 0; stride >>= 1) {
    uint contribution = tid.x + stride;
    GroupMemoryBarrierWithGroupSync();
    uint sum = WaveActiveSum(contribution * 2);
    Shared[0] = sum;
    GroupMemoryBarrierWithGroupSync();
    Out[tid.x] = Shared[0];
  }
}

//--- heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 16
