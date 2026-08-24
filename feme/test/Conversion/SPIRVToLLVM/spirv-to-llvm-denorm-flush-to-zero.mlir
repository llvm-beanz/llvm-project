// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// `VK_KHR_shader_float_controls`'s (roadmap F3) `DenormFlushToZero` execution
// mode is now honored rather than rejected (roadmap F15b): unlike
// `RoundingModeRTZ` (spirv-to-llvm-rounding-mode-rtz.mlir), LLVM has no
// constrained-intrinsics equivalent for flush-to-zero, so each arithmetic FP
// op conversion pattern instead flushes any subnormal operand (and its own
// result) to a same-signed zero in software, via `llvm.is.fpclass`,
// `llvm.copysign` and `llvm.select`, around the plain, round-to-nearest-even
// op MLIR's own pattern would otherwise produce.

// CHECK-NOT: __spv__
// CHECK-LABEL: llvm.func @denorm_flush_to_zero
// CHECK-NOT: strictfp
// CHECK: %[[A_SUBNORMAL:.*]] = "llvm.intr.is.fpclass"(%arg0) <{bit = 144 : i32}> : (f32) -> i1
// CHECK: %[[A_ZERO:.*]] = llvm.mlir.constant(0.000000e+00 : f32) : f32
// CHECK: %[[A_SIGNED_ZERO:.*]] = llvm.intr.copysign(%[[A_ZERO]], %arg0) : (f32, f32) -> f32
// CHECK: %[[A:.*]] = llvm.select %[[A_SUBNORMAL]], %[[A_SIGNED_ZERO]], %arg0 : i1, f32
// CHECK: %[[B_SUBNORMAL:.*]] = "llvm.intr.is.fpclass"(%arg1) <{bit = 144 : i32}> : (f32) -> i1
// CHECK: %[[B_ZERO:.*]] = llvm.mlir.constant(0.000000e+00 : f32) : f32
// CHECK: %[[B_SIGNED_ZERO:.*]] = llvm.intr.copysign(%[[B_ZERO]], %arg1) : (f32, f32) -> f32
// CHECK: %[[B:.*]] = llvm.select %[[B_SUBNORMAL]], %[[B_SIGNED_ZERO]], %arg1 : i1, f32
// CHECK: %[[SUM:.*]] = llvm.fadd %[[A]], %[[B]] : f32
// CHECK: %[[SUM_SUBNORMAL:.*]] = "llvm.intr.is.fpclass"(%[[SUM]]) <{bit = 144 : i32}> : (f32) -> i1
// CHECK: %[[SUM_ZERO:.*]] = llvm.mlir.constant(0.000000e+00 : f32) : f32
// CHECK: %[[SUM_SIGNED_ZERO:.*]] = llvm.intr.copysign(%[[SUM_ZERO]], %[[SUM]]) : (f32, f32) -> f32
// CHECK: %[[FLUSHED_SUM:.*]] = llvm.select %[[SUM_SUBNORMAL]], %[[SUM_SIGNED_ZERO]], %[[SUM]] : i1, f32
// CHECK: llvm.return %[[FLUSHED_SUM]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DenormFlushToZero], []> {
  spirv.func @denorm_flush_to_zero(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "Fragment" @denorm_flush_to_zero
  spirv.ExecutionMode @denorm_flush_to_zero "OriginUpperLeft"
  spirv.ExecutionMode @denorm_flush_to_zero "DenormFlushToZero", 32
}

// -----

// A bit width `DenormFlushToZero` was not declared for keeps using the
// plain, unflushed `llvm.fadd` MLIR's own `DirectConversionPattern`
// produces: this entry point's `DenormFlushToZero` names only the 32-bit
// width, not this op's 16-bit one.
// CHECK-LABEL: llvm.func @denorm_flush_to_zero_wrong_width
// CHECK: llvm.fadd %{{.*}}, %{{.*}} : f16
// CHECK-NOT: is.fpclass
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DenormFlushToZero, Float16], []> {
  spirv.func @denorm_flush_to_zero_wrong_width(%a: f16, %b: f16) -> (f16) "None" {
    %0 = spirv.FAdd %a, %b : f16
    spirv.ReturnValue %0 : f16
  }
  spirv.EntryPoint "Fragment" @denorm_flush_to_zero_wrong_width
  spirv.ExecutionMode @denorm_flush_to_zero_wrong_width "OriginUpperLeft"
  spirv.ExecutionMode @denorm_flush_to_zero_wrong_width "DenormFlushToZero", 32
}

// -----

// `DenormFlushToZero` and `RoundingModeRTZ` (roadmap F15a) may both be
// declared for the same bit width at once: the operands are flushed, the
// operation itself becomes the constrained, round-toward-zero intrinsic
// (rather than the plain `llvm.fadd` a `DenormFlushToZero`-only entry point
// gets), and the result is flushed again -- the two modes' lowering
// strategies compose independently of each other. `strictfp` is still
// required, the same as any other constrained-FP-intrinsic-emitting entry
// point.
// CHECK-LABEL: llvm.func @denorm_flush_to_zero_and_rounding_mode_rtz
// CHECK-SAME: attributes {passthrough = [{{.*}}"strictfp"]}
// CHECK: %[[A:.*]] = llvm.select {{.*}} : i1, f32
// CHECK: %[[B:.*]] = llvm.select {{.*}} : i1, f32
// CHECK: %[[SUM:.*]] = llvm.intr.experimental.constrained.fadd %[[A]], %[[B]] towardzero ignore : f32
// CHECK: llvm.select {{.*}}, %[[SUM]] : i1, f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DenormFlushToZero, RoundingModeRTZ], []> {
  spirv.func @denorm_flush_to_zero_and_rounding_mode_rtz(%a: f32, %b: f32) -> (f32) "None" {
    %0 = spirv.FAdd %a, %b : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "Fragment" @denorm_flush_to_zero_and_rounding_mode_rtz
  spirv.ExecutionMode @denorm_flush_to_zero_and_rounding_mode_rtz "OriginUpperLeft"
  spirv.ExecutionMode @denorm_flush_to_zero_and_rounding_mode_rtz "DenormFlushToZero", 32
  spirv.ExecutionMode @denorm_flush_to_zero_and_rounding_mode_rtz "RoundingModeRTZ", 32
}
