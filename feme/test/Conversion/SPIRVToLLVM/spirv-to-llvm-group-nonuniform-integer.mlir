// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks roadmap H124a (originally L10): `spirv.GroupNonUniformIAdd`
// (`WaveActiveSum`'s own SPIR-V shape) reduced over a *signed* `si32`
// operand -- upstream MLIR's own `GroupReducePattern` (`mlir/lib/
// Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`) passes `si32` straight through
// to the `llvm.call`/`llvm.func` it builds, which the LLVM dialect
// rejects outright (only a signless `i32` is a valid LLVM dialect type)
// -- so this pass's own higher-benefit `GroupNonUniformReducePattern`
// converts the result (and operand) through the type converter first,
// landing on a real `llvm.spv.wave.reduce.sum` intrinsic call instead of
// a raw mangled builtin call, so `feme-cpu-simdize`'s own
// `WaveUniformity`/`SIMDize` passes (which only ever recognize a real
// `IntrinsicInst`) can classify and widen it correctly.

// CHECK-LABEL: llvm.func @non_uniform_iadd_signed(
// CHECK-SAME:                                     %[[ARG:.*]]: i32) -> i32 {
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.reduce.sum"(%[[ARG]]) : (i32) -> i32
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniformArithmetic], []> {
  spirv.func @non_uniform_iadd_signed(%arg0: si32) -> si32 "None" {
    %0 = spirv.GroupNonUniformIAdd <Subgroup> <Reduce> %arg0 : si32 -> si32
    spirv.ReturnValue %0 : si32
  }
}

// -----

// Checks the `BitwiseAnd` variant (`WaveActiveBitAnd`'s own SPIR-V shape)
// also normalizes an unsigned `ui32` operand the same way, and lands on
// `llvm.spv.wave.reduce.and`.

// CHECK-LABEL: llvm.func @non_uniform_bitwise_and_unsigned(
// CHECK-SAME:                                              %[[ARG:.*]]: i32) -> i32 {
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.reduce.and"(%[[ARG]]) : (i32) -> i32
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniformArithmetic], []> {
  spirv.func @non_uniform_bitwise_and_unsigned(%arg0: ui32) -> ui32 "None" {
    %0 = spirv.GroupNonUniformBitwiseAnd <Subgroup> <Reduce> %arg0 : ui32 -> ui32
    spirv.ReturnValue %0 : ui32
  }
}
