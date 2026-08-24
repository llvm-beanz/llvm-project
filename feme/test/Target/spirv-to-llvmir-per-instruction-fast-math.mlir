// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme-translate --import-spirv %t.spv | \
// RUN:   feme-translate --no-implicit-module --spirv-to-llvmir - -o - | FileCheck %s
//
// REQUIRES: spirv-registered-target

// `FPFastMathMode` (`VK_KHR_shader_float_controls2`, roadmap F15c) becomes
// real LLVM fast-math flags on the generated `fadd`, confirmed against the
// actual LLVM IR `mlir::translateModuleToLLVMIR` produces (not just the
// `llvm` dialect this conversion's own IR uses) -- concrete evidence this
// is genuine fast-math codegen, not merely an inert attribute round-tripped
// unchanged.
//
// This stops short of a full round trip back through LLVM's real, in-tree
// SPIRV backend (unlike spirv-backend-per-instruction-rounding-mode.mlir):
// the backend does not currently re-emit an `FPFastMathMode` decoration
// from a callee's fast-math flags at all (confirmed empirically -- the
// round trip silently drops it), a gap in LLVM's own SPIRV backend rather
// than in this conversion, and out of scope for this row to fix.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @fast_math(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b {fp_fast_math_mode = #spirv.fastmath_mode<NotNaN|NotInf|NSZ>} : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "Fragment" @fast_math
  spirv.ExecutionMode @fast_math "OriginUpperLeft"
}

// CHECK: define float @fast_math(float %{{.*}}, float %{{.*}})
// CHECK: fadd nnan ninf nsz float
