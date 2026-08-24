// RUN: not feme-opt --feme-convert-spirv-to-llvm %s 2>&1 | FileCheck %s

// `VK_KHR_shader_float_controls` (roadmap F3) `DenormFlushToZero` and
// `RoundingModeRTZ` (see spirv-to-llvm-rounding-mode-rtz-invalid.mlir) ask
// for behavior no floating-point op conversion pattern ever produces (they
// never flush denormals or truncate rather than round-to-nearest-even), so
// declaring either is diagnosed up front rather than silently dropped and
// miscompiled the way spirv-to-llvm-float-controls.mlir shows the
// compatible modes (`DenormPreserve`, `RoundingModeRTE`,
// `SignedZeroInfNanPreserve`) are.

// CHECK: error: {{.*}}execution mode 'DenormFlushToZero' is not supported
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DenormFlushToZero], []> {
  spirv.func @denorm_flush_to_zero() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @denorm_flush_to_zero
  spirv.ExecutionMode @denorm_flush_to_zero "OriginUpperLeft"
  spirv.ExecutionMode @denorm_flush_to_zero "DenormFlushToZero", 32
}
