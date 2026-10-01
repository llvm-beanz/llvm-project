// REQUIRES: system-dxc
// RUN: split-file %s %t
// RUN: %dxc -T cs_6_0 -E main -spirv -fspv-target-env=vulkan1.3 -Fo %t/shader.spv %t/shader.hlsl
// RUN: feme-run --groups=1,1,1 --heap=%t/heap.yaml %t/shader.spv | FileCheck %s

// Roadmap L319's own follow-up ("image-bindings"/"sampler-bindings" heap
// YAML keys, see feme/docs/Roadmap.md's §2.6.1 and feme/docs/
// FeMeCPUDesign.md's "Bound-resource normalization"): before this
// feature existed, the heap YAML's `images`/`samplers` entries could only
// describe the *dynamic*, bindless image/sampler heap
// (`ResourceDescriptorHeap`/`SamplerDescriptorHeap` access) -- there was no
// way to supply a host descriptor for a traditionally-bound
// (`register(tN, spaceM)`) image or sampler. Every such handle resolved to
// that bound range's zero-initialized reserved-heap prefix instead of any
// host-supplied descriptor, so a real shader genuinely sampling/loading a
// traditionally-bound `Texture1D` through `feme-run` silently produced
// all-zero texels rather than any diagnosable error. This test's own
// `Texture1D<float4> : register(t0, space1)` -- real SPIR-V, compiled by
// DXC, imported by `feme::SPIRVImporter`, then raised and lowered by
// `feme::cpu::SPIRVResourceLoweringPass` -- is the first end-to-end
// coverage of a traditionally-bound image resolving to a real,
// host-supplied descriptor rather than the empty reserved prefix.

// Each lane loads its own texel (mip 0, `(tid, 0)`) out of the bound
// `Texture1D` and copies it verbatim into the output buffer; a non-zero
// result here is this test's whole contract.
// CHECK: binding[0:0][0]: 1065353216 1073741824 1077936128 1082130432 1073741824 1082130432 1086324736 1090519040 1077936128 1086324736 1091567616 1094713344 1082130432 1090519040 1094713344 1097859072

//--- shader.hlsl
Texture1D<float4> Tex : register(t0, space1);
RWStructuredBuffer<float4> Out : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  Out[tid.x] = Tex.Load(int2(tid.x, 0));
}

//--- heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        size: 64
image-bindings:
  - space: 1
    register: 0
    entries:
      - index: 0
        dimension: 1d
        extent: [4, 1]
        format: r32g32b32a32_float
        data: [1065353216, 1073741824, 1077936128, 1082130432,
               1073741824, 1082130432, 1086324736, 1090519040,
               1077936128, 1086324736, 1091567616, 1094713344,
               1082130432, 1090519040, 1094713344, 1097859072]
