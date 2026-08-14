// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: %feme-wave-size-sweep --feme-run=feme-run --filecheck=FileCheck \
// RUN:     --check-file=%s --wave-sizes=4,8,16,32 -- --groups=1,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer

// End-to-end coverage for masking a scalarized atomic's per-lane execution:
// real HLSL, compiled to a DXIL DXContainer by Clang, imported/raised/
// JIT-dispatched by `feme-run` (see loop.hlsl's own comment for the
// pipeline this exercises in full). This is `histogram.hlsl` from roadmap
// step R2 (feme/docs/Roadmap.md's §2.3): "divergent atomics into a shared
// buffer... the scalarization fallback's only realistic workload, and the
// one that catches §1.6's unmasked-lane P0" -- an `atomicrmw` has no vector
// form, so `feme::cpu::SIMDizePass` falls back to scalarizing it (`W`
// per-lane clones, see `simdize-scalarize-atomic.ll`), and before this
// milestone that fallback ran every clone unconditionally, regardless of
// whether a divergent branch should have excluded a given lane from
// running it at all (a masked *load*/*store* already got this right; an
// atomic did not -- see the Status section's milestone 7 deviation note in
// feme/docs/FeMeCPUDesign.md). Only lanes with an even `tid.x` increment
// `Counter` (`InterlockedAdd`'s own return value -- captured directly,
// rather than a separate reload, to stay clear of the still-open "divergent
// (per-lane) groupshared address" narrowing a *nested* histogram bucket
// would otherwise hit -- see §1.6's "Divergent groupshared access is
// diagnosed" row) and get back the running count *before* their own
// increment (their "slot", the classic atomic-append-buffer pattern real
// histogram/stream-compaction shaders use); an odd lane keeps the sentinel
// `999` `slot` was initialized to. Before this milestone's fix, every lane
// -- including the odd ones that should never have touched `Counter` at
// all -- would have executed the real, unmasked increment, corrupting
// every subsequent lane's slot (a silently wrong answer, not a crash, per
// §1.6's P0 rationale). Wave-size-independent (see loop.hlsl's own comment
// for why this runs at `W` in {4, 8, 16, 32} -- roadmap step R1,
// feme/docs/Roadmap.md's §2.2.1): dispatch is still sequential per lane
// (§1.6's own P1 narrowing), so `Counter`'s increments happen in `tid.x`
// order regardless of `W`.

// CHECK: binding[0:0][0]: 0 999 1 999

//--- shader.hlsl
RWStructuredBuffer<uint> Out : register(u0);
groupshared uint Counter;

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  Counter = 0;
  uint slot = 999;
  if (tid.x % 2 == 0) {
    InterlockedAdd(Counter, 1, slot);
  }
  Out[tid.x] = slot;
}

//--- heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 16
