// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// A single `spirv.module` is legally allowed to carry entry points of more
// than one stage at once: a `VkShaderModule` is created once and may be
// referenced by several `VkPipelineShaderStageCreateInfo`s naming different
// `pName` entries, possibly of different stages -- real `dEQP-VK.
// spirv_assembly.instruction.compute.{multiple_shaders,unused_variables,
// opname,opunreachable}.*` CTS modules do exactly this shape (one
// `GLCompute`-model entry actually dispatched alongside an unrelated,
// never-selected `Vertex`-model one in the same module). Each entry point's
// own stage is recorded independently -- there is no such thing as "the
// module's" single stage to cross-check an individual entry point against
// ("Stage identity" in feme/docs/FeMeGraphicsDesign.md, updated to describe
// this); per-entry stage validation (against whichever single
// `VkPipelineShaderStageCreateInfo::stage` actually names it) happens later,
// at pipeline-creation time (`Pipeline.cpp`'s `selectEntryPoint`), not here.

// CHECK: llvm.func @compute_entry() attributes {passthrough = {{.*}}"hlsl.shader", "compute"{{.*}}"feme.shader.stage", "compute"{{.*}}}
// CHECK: llvm.func @vertex_entry() attributes {passthrough = {{.*}}"hlsl.shader", "vertex"{{.*}}"feme.shader.stage", "vertex"{{.*}}}
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @compute_entry() -> () "None" {
    spirv.Return
  }
  spirv.func @vertex_entry() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @compute_entry
  spirv.EntryPoint "Vertex" @vertex_entry
}
