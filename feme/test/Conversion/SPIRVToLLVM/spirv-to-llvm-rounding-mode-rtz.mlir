// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// `VK_KHR_shader_float_controls`'s (roadmap F3) `RoundingModeRTZ` execution
// mode is now honored rather than rejected (roadmap F15a): the arithmetic FP
// op conversion patterns route the bit width(s) it names through
// `llvm.experimental.constrained.*` intrinsics, and the entry point gains
// `strictfp` (the function attribute LLVM's verifier requires of any
// function containing a constrained-FP-intrinsic call). See
// spirv-to-llvm-denorm-flush-to-zero.mlir for `DenormFlushToZero` (roadmap
// F15b), the one `VK_KHR_shader_float_controls` mode this uses a materially
// different, software-flush lowering strategy for rather than a constrained
// intrinsic.
//
// (Roadmap L214) Each constrained op below is bracketed by
// `llvm.get.rounding`/`llvm.set.rounding` and itself carries `dynamic`, not
// a static `towardzero`, rounding-mode metadata: an isolated `.ll`
// reproducer confirmed this host's AArch64 backend silently discards a
// *static* non-default rounding mode on a constrained arithmetic intrinsic
// (no `FPCR` manipulation at all, same bug class as roadmap L208 found for
// `FConvert`), whereas `llvm.set.rounding` followed by a `dynamic`-mode
// constrained op reliably emits real `FPCR` read/modify/write codegen
// around an ordinary hardware instruction and -- unlike a plain,
// unconstrained op -- is never constant-folded away under an assumed
// default rounding mode.
// CHECK-NOT: __spv__
// CHECK-LABEL: llvm.func @rounding_mode_rtz
// CHECK-SAME: attributes {passthrough = [{{.*}}"strictfp"]}
// CHECK: %[[OLD0:.*]] = llvm.call_intrinsic "llvm.get.rounding"()
// CHECK: %[[RTZ0:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[RTZ0]])
// CHECK: %[[SUM:.*]] = llvm.intr.experimental.constrained.fadd %{{.*}}, %{{.*}} dynamic ignore : f32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[OLD0]])
// CHECK: %[[OLD1:.*]] = llvm.call_intrinsic "llvm.get.rounding"()
// CHECK: %[[RTZ1:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[RTZ1]])
// CHECK: %[[DIFF:.*]] = llvm.intr.experimental.constrained.fsub %[[SUM]], %{{.*}} dynamic ignore : f32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[OLD1]])
// CHECK: %[[OLD2:.*]] = llvm.call_intrinsic "llvm.get.rounding"()
// CHECK: %[[RTZ2:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[RTZ2]])
// CHECK: %[[PROD:.*]] = llvm.intr.experimental.constrained.fmul %[[DIFF]], %{{.*}} dynamic ignore : f32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[OLD2]])
// CHECK: %[[OLD3:.*]] = llvm.call_intrinsic "llvm.get.rounding"()
// CHECK: %[[RTZ3:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[RTZ3]])
// CHECK: %[[QUOT:.*]] = llvm.intr.experimental.constrained.fdiv %[[PROD]], %{{.*}} dynamic ignore : f32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[OLD3]])
// CHECK: %[[OLD4:.*]] = llvm.call_intrinsic "llvm.get.rounding"()
// CHECK: %[[RTZ4:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[RTZ4]])
// CHECK: llvm.intr.experimental.constrained.frem %[[QUOT]], %{{.*}} dynamic ignore : f32
// CHECK: llvm.call_intrinsic "llvm.set.rounding"(%[[OLD4]])
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
