// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `VK_KHR_shader_float_controls` (roadmap F3) execution modes
// this conversion's floating-point op patterns already honor by construction
// -- `DenormPreserve`, `RoundingModeRTE` and `SignedZeroInfNanPreserve`,
// which describe the strict, denormal-preserving, round-to-nearest-even
// code every FP op pattern produces by default -- are accepted and dropped
// like any other `spirv.ExecutionMode`, at every declared bit width, rather
// than rejected the way spirv-to-llvm-denorm-flush-to-zero-invalid.mlir
// shows `DenormFlushToZero` (roadmap F15b) is. `RoundingModeRTZ` (roadmap
// F3/F15a) is also honored now, not merely accepted-and-dropped -- see
// spirv-to-llvm-rounding-mode-rtz.mlir.

// CHECK-NOT: __spv__
// CHECK: llvm.func @denorm_preserve()
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DenormPreserve], []> {
  spirv.func @denorm_preserve() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @denorm_preserve
  spirv.ExecutionMode @denorm_preserve "OriginUpperLeft"
  spirv.ExecutionMode @denorm_preserve "DenormPreserve", 16
  spirv.ExecutionMode @denorm_preserve "DenormPreserve", 32
  spirv.ExecutionMode @denorm_preserve "DenormPreserve", 64
}

// -----

// CHECK-NOT: __spv__
// CHECK: llvm.func @rounding_mode_rte()
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, RoundingModeRTE], []> {
  spirv.func @rounding_mode_rte() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @rounding_mode_rte
  spirv.ExecutionMode @rounding_mode_rte "OriginUpperLeft"
  spirv.ExecutionMode @rounding_mode_rte "RoundingModeRTE", 32
}

// -----

// CHECK-NOT: __spv__
// CHECK: llvm.func @signed_zero_inf_nan_preserve()
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, SignedZeroInfNanPreserve], []> {
  spirv.func @signed_zero_inf_nan_preserve() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Fragment" @signed_zero_inf_nan_preserve
  spirv.ExecutionMode @signed_zero_inf_nan_preserve "OriginUpperLeft"
  spirv.ExecutionMode @signed_zero_inf_nan_preserve "SignedZeroInfNanPreserve", 32
}
