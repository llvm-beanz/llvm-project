// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme-run --wave-size=4 --groups=1,1,1 \
// RUN:     --heap=%t/heap.yaml %t/shader.dxcontainer | FileCheck %s

// End-to-end coverage for a real, formatted typed buffer (see loop.hlsl's
// own comment for the pipeline this exercises in full): every other
// executing HLSL test uses an unstructured raw or structured buffer,
// because that was all the heap YAML's `bindings`/`resource-heap` entries
// could describe (see "Resource shapes" in feme/docs/Roadmap.md's §2.2).
// Roadmap step R8 adds the `kind`/`format`/`stride` heap YAML keys
// (§2.4.3) this test's `heap.yaml` uses, so a `RWBuffer<float4>` -- whose
// element is `<4 x float>`, `ResourceFormat::R32G32B32A32_FLOAT`'s
// identity format, "Descriptor formats" in feme/docs/FeMeCPUDesign.md's
// running example -- can be described precisely instead of as an untyped
// raw byte buffer, and so `femeCpuResourceLoadTypedV4F32`/
// `femeCpuResourceStoreTypedV4F32` (feme/runtime/CPU/FeMeRuntimeCPU.c) get
// their first end-to-end execution coverage through a real DXIL-derived
// typed-buffer access rather than only `FeMeRuntimeCPUTests`' own
// hand-built call.

// Each lane's element is little-endian `float` bit patterns for
// (tid, 2*tid, 3*tid, 4*tid); lane 0 writes four exact zeros.
// CHECK: binding[0:0][0]: 0 0 0 0 1065353216 1073741824 1077936128 1082130432 1073741824 1082130432 1086324736 1090519040 1077936128 1086324736 1091567616 1094713344

//--- shader.hlsl
RWBuffer<float4> Out : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  float v = (float)tid.x;
  Out[tid.x] = float4(v, v * 2.0, v * 3.0, v * 4.0);
}

//--- heap.yaml
bindings:
  - space: 0
    register: 0
    entries:
      - index: 0
        kind: typed-buffer
        format: r32g32b32a32_float
        size: 64
