// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// `VK_KHR_shader_float_controls`'s (roadmap F3) `RoundingModeRTZ` execution
// mode is now honored rather than rejected (roadmap F15a): the arithmetic FP
// op conversion patterns route the bit width(s) it names through
// `llvm.experimental.constrained.*` intrinsics with an explicit
// round-toward-zero rounding mode, and the entry point gains `strictfp` (the
// function attribute LLVM's verifier requires of any function containing a
// constrained-FP-intrinsic call). See spirv-to-llvm-denorm-flush-to-zero-invalid.mlir
// for the one `VK_KHR_shader_float_controls` mode (`DenormFlushToZero`) that
// remains rejected outright (roadmap F15b).

// CHECK-NOT: __spv__
// CHECK-LABEL: llvm.func @rounding_mode_rtz
// CHECK-SAME: attributes {passthrough = [{{.*}}"strictfp"]}
// CHECK: %[[SUM:.*]] = llvm.intr.experimental.constrained.fadd %{{.*}}, %{{.*}} towardzero ignore : f32
// CHECK: %[[DIFF:.*]] = llvm.intr.experimental.constrained.fsub %[[SUM]], %{{.*}} towardzero ignore : f32
// CHECK: %[[PROD:.*]] = llvm.intr.experimental.constrained.fmul %[[DIFF]], %{{.*}} towardzero ignore : f32
// CHECK: %[[QUOT:.*]] = llvm.intr.experimental.constrained.fdiv %[[PROD]], %{{.*}} towardzero ignore : f32
// CHECK: llvm.intr.experimental.constrained.frem %[[QUOT]], %{{.*}} towardzero ignore : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, RoundingModeRTZ], []> {
  spirv.func @rounding_mode_rtz(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b : f32
    %1 = spirv.FSub %0, %b : f32
    %2 = spirv.FMul %1, %b : f32
    %3 = spirv.FDiv %2, %b : f32
    %4 = spirv.FRem %3, %b : f32
    spirv.ReturnValue %4 : f32
  }
  spirv.EntryPoint "Fragment" @rounding_mode_rtz
  spirv.ExecutionMode @rounding_mode_rtz "OriginUpperLeft"
  spirv.ExecutionMode @rounding_mode_rtz "RoundingModeRTZ", 32
}

// -----

// A bit width `RoundingModeRTZ` was not declared for keeps using the plain,
// round-to-nearest-even `llvm.fadd` MLIR's own `DirectConversionPattern`
// produces, even though the entry point (having declared the mode for a
// different width) still gains `strictfp`: this entry point's
// `RoundingModeRTZ` names only the 32-bit width, not this op's 16-bit one.
// CHECK-LABEL: llvm.func @rounding_mode_rtz_wrong_width
// CHECK-SAME: attributes {passthrough = [{{.*}}"strictfp"]}
// CHECK: llvm.fadd %{{.*}}, %{{.*}} : f16
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, RoundingModeRTZ, Float16], []> {
  spirv.func @rounding_mode_rtz_wrong_width(%a: f16, %b: f16) -> (f16) "None" {
    %0 = spirv.FAdd %a, %b : f16
    spirv.ReturnValue %0 : f16
  }
  spirv.EntryPoint "Fragment" @rounding_mode_rtz_wrong_width
  spirv.ExecutionMode @rounding_mode_rtz_wrong_width "OriginUpperLeft"
  spirv.ExecutionMode @rounding_mode_rtz_wrong_width "RoundingModeRTZ", 32
}
