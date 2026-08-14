// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: %feme-wave-size-sweep --feme-run=feme-run --filecheck=FileCheck \
// RUN:     --check-file=%s --wave-sizes=4,8,16,32 -- --groups=1,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer

// End-to-end coverage (see the "Tooling and Testing" section of
// feme/docs/FeMeCPUDesign.md) for a loop: real HLSL, compiled to a DXIL
// DXContainer by Clang's own HLSL front end and DirectX backend (exactly
// the pipeline `feme-dxil-to-dxil.ll` et al. validate `feme`'s own DXIL
// import/export against, see feme/docs/Design.md's "DXIL" section), then
// imported, raised, and JIT-dispatched by `feme-run` itself -- see that
// tool's own file comment for the DXIL-import wiring this exercises. The
// `register(u0)` binding -- the only kind Clang's HLSL front end can emit
// today -- is normalized by `feme::cpu::BoundResourceNormalizationPass`
// (roadmap milestone 11) into the CPU target's bindless heap; the heap
// YAML's `bindings` entry (see feme-run's own file comment) supplies its
// descriptor. `feme::cpu::SIMDizePass` widens a loop as of roadmap
// milestone 7; this counts up a per-lane sum that depends on the loop trip
// count actually running to completion. Neither this shader's own
// computation nor its `numthreads`-derived group shape depends on the
// wave's own width, so roadmap step R1 (see feme/docs/Roadmap.md's §2.2.1)
// runs it at `W` in {4, 8, 16, 32}: a widening bug that only shows up away
// from the tree's overwhelmingly common `W = 4` is caught the same way a
// translation bug is.

// CHECK: binding[0:0][0]: 10 20 30 40

//--- shader.hlsl
RWStructuredBuffer<uint> Out : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint sum = 0;
  for (uint i = 0; i < 4; i++) {
    sum += (tid.x + 1) * (i + 1);
  }
  Out[tid.x] = sum;
}

//--- heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 16
