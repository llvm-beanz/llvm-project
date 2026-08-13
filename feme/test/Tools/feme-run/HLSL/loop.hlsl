// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme-run --dxil-bind-register-resources --wave-size=4 --groups=1,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s

// End-to-end coverage (see the "Tooling and Testing" section of
// feme/docs/FeMeCPUDesign.md) for a loop: real HLSL, compiled to a DXIL
// DXContainer by Clang's own HLSL front end and DirectX backend (exactly
// the pipeline `feme-dxil-to-dxil.ll` et al. validate `feme`'s own DXIL
// import/export against, see feme/docs/Design.md's "DXIL" section), then
// imported, raised, and JIT-dispatched by `feme-run` itself -- see that
// tool's own file comment for the DXIL-import wiring this exercises, and
// `--dxil-bind-register-resources`'s own `cl::desc` for why a `register()`
// binding (the only kind Clang's HLSL front end can emit today) is bridged
// onto the CPU target's bindless heap. `feme::cpu::SIMDizePass` widens a
// loop as of roadmap milestone 7; this counts up a per-lane sum that
// depends on the loop trip count actually running to completion.

// CHECK: heap[0]: 10 20 30 40

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
resource-heap:
  - index: 0
    size: 16
