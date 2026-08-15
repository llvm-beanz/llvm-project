// REQUIRES: directx-registered-target
// RUN: split-file %s %t
// RUN: clang -target dxil--shadermodel6.5-compute -c %t/shader.hlsl -o %t/shader.dxcontainer
// RUN: feme --target=dxil %t/shader.dxcontainer -o %t/roundtrip.dxcontainer
// RUN: feme-run --wave-size=4 --groups=1,1,1 \
// RUN:     --heap=%t/heap.yaml %t/roundtrip.dxcontainer | FileCheck %s

// Roadmap step R14 (see feme/docs/Roadmap.md's §2.2.6 "Round trips"):
// feme-dxil-to-dxil.ll (feme/test/Tools/feme) already checks that a
// DXContainer retargeted to `dxil` through the full `feme` CLI (import ->
// `feme::dxil::OpRaisingPass`/`MetadataRaisingPass` -> re-export) produces
// *another* DXContainer, but only ever inspects its first four magic bytes
// -- nothing re-imports that output and runs it, which is a much weaker
// statement than "the retargeted container still computes the same
// answer". This test is that missing link: `feme --target=dxil` writes
// `%t/roundtrip.dxcontainer`, and `feme-run` then imports *that* file --
// re-running `feme::DXILImporter` and the raising passes a *second* time
// on a container that has itself already been through them once -- and
// dispatches the result through the JIT.
//
// The shader itself is typed-buffer.hlsl's own (see that test's own
// comment for the `RWBuffer<float4>`/heap-YAML `kind`/`format` coverage
// this reuses): compiling real HLSL rather than hand-writing `.ll`
// matters here specifically, since a hand-written raw-buffer
// `llvm.dx.resource.store.rawbuffer` call with a non-`poison` element
// index (`feme-run`'s own thread-id-store.ll shape) turned out not to
// round-trip through `feme --target=dxil`'s re-export at all -- LLVM's own
// `DXILBitcodeWriter` asserts on it ("Element index of raw buffer must be
// poison") -- while Clang's own DXIL backend never emits that shape in the
// first place, so a typed-buffer store compiled from real HLSL is what
// this milestone's own re-export path actually needs to round-trip.

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
