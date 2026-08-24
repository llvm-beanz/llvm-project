// RUN: not feme-opt --feme-convert-spirv-to-llvm %s 2>&1 | FileCheck %s

// See spirv-to-llvm-denorm-flush-to-zero-invalid.mlir: `RoundingModeRTZ` is
// the other `VK_KHR_shader_float_controls` (roadmap F3) execution mode this
// conversion cannot honor, and is diagnosed the same way.

// CHECK: error: {{.*}}execution mode 'RoundingModeRTZ' is not supported
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, RoundingModeRTZ], []> {
  spirv.func @rounding_mode_rtz() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @rounding_mode_rtz
  spirv.ExecutionMode @rounding_mode_rtz "OriginUpperLeft"
  spirv.ExecutionMode @rounding_mode_rtz "RoundingModeRTZ", 16
}
