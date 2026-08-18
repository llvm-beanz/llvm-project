// REQUIRES: system-dxc
// RUN: split-file %s %t
// RUN: %dxc -T cs_6_0 -E main -spirv -fspv-target-env=vulkan1.3 -Fo %t/shader.spv %t/shader.hlsl
// RUN: feme-run --groups=1,1,1 --heap=%t/heap.yaml %t/shader.spv | FileCheck %s

// End-to-end coverage for roadmap milestone V0.5 (feme/docs/Roadmap.md):
// real HLSL, compiled to a genuine SPIR-V binary by Microsoft's own DXC --
// not `llc`'s SPIR-V backend, which never emits the structured
// `OpSelectionMerge`/`OpLoopMerge` shape a real Vulkan compiler does -- then
// imported (`feme::SPIRVImporter`), translated, and JIT-dispatched by
// `feme-run` (see that tool's own file comment for the SPIR-V import
// wiring this exercises). This is one entry in the "glslang/DXC/Clang
// corpus" milestone V0.5 asks for; see `SPIRV/loop-merge-phi.hlsl` in this
// directory for the loop-with-`break` shape that motivated the milestone in
// the first place, and feme/docs/FeMeVulkanDesign.md's "SPIR-V import
// prerequisites" for the decision this corpus validates.
//
// This shader's `if`/`else` merge produces exactly the `OpPhi` shape that
// exercises `SPIRVResourceLoweringPass`/`feme::cpu::PreparePass`'s
// diamond flattening end-to-end with a real compiler's output, not a
// hand-written fixture.

// CHECK: binding[0:0][0]: 100 101 20 30

//--- shader.hlsl
RWStructuredBuffer<uint> Out : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint v;
  if (tid.x > 1) {
    v = tid.x * 10;
  } else {
    v = tid.x + 100;
  }
  Out[tid.x] = v;
}

//--- heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 16
