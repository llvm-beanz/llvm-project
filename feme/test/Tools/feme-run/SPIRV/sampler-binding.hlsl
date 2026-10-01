// REQUIRES: system-dxc
// RUN: split-file %s %t
// RUN: %dxc -T cs_6_0 -E main -spirv -fspv-target-env=vulkan1.3 -Fo %t/shader.spv %t/shader.hlsl
// RUN: feme-run --reference --groups=1,1,1 --heap=%t/heap.yaml %t/shader.spv | FileCheck %s

// The `sampler-bindings` counterpart to `SPIRV/image-binding.hlsl`'s own
// `image-bindings` coverage (roadmap L319's own follow-up, see
// feme/docs/Roadmap.md's §2.6.1): before this feature existed, a
// traditionally-bound (`register(sN, spaceM)`) `SamplerState` had no way
// to get a host-supplied descriptor from the heap YAML either -- it
// resolved to the same zero-initialized reserved prefix an unbound image
// did. This test exercises a real `Texture1D<float>::SampleLevel` call --
// not just a `Load` -- against both a traditionally-bound image *and* a
// traditionally-bound sampler simultaneously, using point (`nearest`)
// filtering at each texel's own center so the expected result is each
// source texel's exact bit pattern, not a blended value that would also
// depend on the filtering math under test elsewhere.

// Each lane samples its own texel's center (`(tid + 0.5) / 4`) and the
// point-filtered result is that texel's own exact bit pattern.
// CHECK: binding[3:0][0]: 1045220557 1053609165 1058642330 1061997773

//--- shader.hlsl
Texture1D<float> Tex : register(t0, space1);
SamplerState Samp : register(s0, space2);
RWStructuredBuffer<float> Out : register(u0, space3);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  float u = (float(tid.x) + 0.5) / 4.0;
  float r = Tex.SampleLevel(Samp, u, 0.0);
  Out[tid.x] = r;
}

//--- heap.yaml
bindings:
  - space: 3
    register: 0
    entries:
      - index: 0
        size: 16
image-bindings:
  - space: 1
    register: 0
    entries:
      - index: 0
        dimension: 1d
        extent: [4, 1]
        format: r32_float
        data: [1045220557, 1053609165, 1058642330, 1061997773]
sampler-bindings:
  - space: 2
    register: 0
    entries:
      - index: 0
        min-filter: nearest
        mag-filter: nearest
        mip-filter: nearest
        address-u: clamp-to-edge
