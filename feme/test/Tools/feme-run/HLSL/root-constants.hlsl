// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme-run --wave-size=4 --groups=1,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s

// End-to-end coverage for root constants (roadmap step R12; see "Root
// constants" in feme/docs/FeMeCPUDesign.md): a `cbuffer` bound at the one
// recognized binding, `(b0, space0)`, is lowered by
// `feme::cpu::ResourceLoweringPass` (this shader also uses a bindless
// `RWStructuredBuffer`, so `feme::cpu::RootConstantLoweringPass` itself
// leaves the two co-existing, reusing that pass's own env parameters
// instead -- see RootConstantLowering.h's file comment) into a
// bounds-checked load from the CPU ABI's inline root-constant block, which
// `feme-run`'s heap YAML supplies via its `root-constants` field. Every
// lane reads the same `Value.x` (a uniform root-constant load) and adds
// its own `tid.x`.

// CHECK: binding[0:0][0]: 100 101 102 103

//--- shader.hlsl
cbuffer RootConsts : register(b0) {
  uint4 Value;
};
RWStructuredBuffer<uint> Out : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  Out[tid.x] = Value.x + tid.x;
}

//--- heap.yaml
root-constants: [100, 0, 0, 0]
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 16
