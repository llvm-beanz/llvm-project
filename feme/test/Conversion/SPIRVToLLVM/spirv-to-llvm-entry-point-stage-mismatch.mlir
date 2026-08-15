// RUN: not feme-opt --feme-convert-spirv-to-llvm %s 2>&1 | FileCheck %s

// A converted module carries one target triple, whose environment names the
// pipeline stage it implements, and every entry point's own
// `feme.shader.stage` is validated against it ("Stage identity" in
// feme/docs/FeMeGraphicsDesign.md). A module declaring entry points of two
// different stages has no such single environment, so it is diagnosed rather
// than converted with a triple describing only the first of them.

// CHECK: error: {{.*}}entry point 'vertex_entry' declares stage 'vertex', which disagrees with the module's target triple 'spirv-unknown-vulkan-compute'
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
