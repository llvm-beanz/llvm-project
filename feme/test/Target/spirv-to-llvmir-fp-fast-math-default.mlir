// RUN: feme-translate --no-implicit-module --serialize-spirv %s -o %t.spv
// RUN: feme-translate --import-spirv %t.spv | \
// RUN:   feme-translate --no-implicit-module --spirv-to-llvmir - -o - | FileCheck %s
//
// REQUIRES: spirv-registered-target

// `FPFastMathDefault` (`VK_KHR_shader_float_controls2`, roadmap F15d): an
// entry point's own per-type default `FPFastMathMode` applies to every
// arithmetic op of that type lacking a `fp_fast_math_mode` decoration of
// its own, confirmed against the actual LLVM IR
// `mlir::translateModuleToLLVMIR` produces -- `%0` (f32) picks up the
// entry point's own `f32` default (`NotNaN|NotInf`, i.e. mask `3`), while
// `%1` (also f32) keeps its own, different decoration instead -- the same
// "decoration overrides entry-point-wide default" precedence
// `FPRoundingMode` already has over `RoundingModeRTZ` (roadmap
// F15a/F15c). `%2` (f16) has no default declared for its own width at
// all, and picks up neither.
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader, FloatControls2], [SPV_KHR_float_controls2]> {
  spirv.func @fast_math_default(%a: f32, %b: f32, %c: f16, %d: f16) -> () "None" {
    %0 = spirv.FAdd %a, %b : f32
    %1 = spirv.FMul %a, %b {fp_fast_math_mode = #spirv.fastmath_mode<AllowRecip>} : f32
    %2 = spirv.FAdd %c, %d : f16
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @fast_math_default
  spirv.ExecutionMode @fast_math_default "OriginUpperLeft"
  spirv.ExecutionModeId @fast_math_default "FPFastMathDefault" f32, 3
}

// CHECK: define void @fast_math_default
// CHECK: fadd nnan ninf float
// CHECK: fmul arcp float
// CHECK: fadd half
