// RUN: not feme-opt --feme-convert-spirv-to-llvm %s 2>&1 | FileCheck %s

// `VK_KHR_shader_float_controls`'s (roadmap F3) `DenormFlushToZero` asks for
// behavior no floating-point op conversion pattern can produce (LLVM's
// `denormal-fp-math`/`denormal-fp-math-f32` function attributes cover only
// `f32`, not the `f16`/`f64` widths this mode may equally name -- roadmap
// F15b), so declaring it is diagnosed up front rather than silently dropped
// and miscompiled the way spirv-to-llvm-float-controls.mlir shows the
// compatible modes (`DenormPreserve`, `RoundingModeRTE`,
// `SignedZeroInfNanPreserve`) are, and spirv-to-llvm-rounding-mode-rtz.mlir
// shows `RoundingModeRTZ` (roadmap F15a, now honored rather than rejected)
// is.

// CHECK: error: {{.*}}execution mode 'DenormFlushToZero' is not supported
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DenormFlushToZero], []> {
  spirv.func @denorm_flush_to_zero() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @denorm_flush_to_zero
  spirv.ExecutionMode @denorm_flush_to_zero "OriginUpperLeft"
  spirv.ExecutionMode @denorm_flush_to_zero "DenormFlushToZero", 32
}
