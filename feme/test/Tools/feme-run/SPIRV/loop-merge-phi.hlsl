// REQUIRES: system-dxc
// RUN: split-file %s %t
// RUN: %dxc -T cs_6_0 -E main -spirv -fspv-target-env=vulkan1.3 -Fo %t.spv %t/shader.hlsl
// RUN: feme-translate --import-spirv --no-implicit-module --spirv-to-llvmir %t.spv | FileCheck %s

// The shader roadmap milestone V0.5 exists because of: real HLSL, compiled to
// a genuine SPIR-V binary by DXC, with a loop whose `break` carries a value
// out through the loop's merge block -- exactly the `OpPhi`-in-loop-merge
// shape MLIR's structured SPIR-V deserializer rejects outright (see
// feme/docs/FeMeVulkanDesign.md's "SPIR-V import prerequisites"). Before this
// milestone, `feme::SPIRVImporter` failed to import this shader at all.
//
// This is entry two of the "glslang/DXC/Clang corpus" milestone V0.5 asks
// for (see `diamond.hlsl` in this directory for entry one, an `if`/`else`
// merge that already JIT-dispatches end to end). This one stops at raised
// LLVM IR rather than a JIT dispatch: `feme::cpu::LinearizePass`'s loop
// linearizer only supports a narrow set of restructured loop shapes today
// (a pre-existing, format-independent limitation -- reproduced with
// hand-written LLVM IR carrying the identical CFG shape, see
// `feme/test/Import/SPIRV/spirv-import-unstructured-default.ll`'s sibling
// investigation notes in agent_thoughts.md), so a full dispatch of this
// exact shader is left to whichever roadmap step widens that matcher.
// Checking the raised IR here still proves the thing this milestone is
// actually about: the importer and `spirv` -> `llvm` dialect/LLVM IR
// translation both survive this shape end-to-end, unstructured branches
// and all -- ready for `feme::cpu::PreparePass`'s existing restructurer
// (already exercised on DXIL's naturally unstructured CFGs) to consume once
// that separate loop-shape limitation is widened.

// CHECK: br label
// CHECK: phi i32
// CHECK: icmp ult i32
// CHECK: icmp ugt i32
// CHECK: llvm.spv.resource.handlefrombinding

//--- shader.hlsl
RWStructuredBuffer<uint> Out : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint result = 0;
  for (uint i = 0; i < 100; i++) {
    if (i > 10) {
      result = i;
      break;
    }
    result = i + 1;
  }
  Out[0] = result;
}
