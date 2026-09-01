// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks roadmap L10: `spirv.GroupNonUniformIAdd` (`WaveActiveSum`'s own
// SPIR-V shape) reduced over a *signed* `si32` operand -- upstream MLIR's
// own `GroupReducePattern` (`mlir/lib/Conversion/SPIRVToLLVM/
// SPIRVToLLVM.cpp`) passes `si32` straight through to the `llvm.call`/
// `llvm.func` it builds, which the LLVM dialect rejects outright (only a
// signless `i32` is a valid LLVM dialect type) -- so this pass's own
// higher-benefit `IntegerGroupNonUniformReducePattern` converts the
// result (and operand) through the type converter first, landing on the
// exact same mangled builtin name upstream's signless `i32` case already
// uses (matching the SPIR-V backend's own recognized builtin, which does
// not distinguish `int`/`uint` operands either).

// CHECK-LABEL: llvm.func spir_funccc @_Z27__spirv_GroupNonUniformIAddiij(i32, i32, i32) -> i32 attributes {convergent, no_unwind, will_return}
// CHECK-LABEL: llvm.func @non_uniform_iadd_signed(
// CHECK-SAME:                                     %[[ARG:.*]]: i32) -> i32 {
// CHECK: %[[SCOPE:.*]] = llvm.mlir.constant(3 : i32) : i32
// CHECK: %[[GROUPOP:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[RESULT:.*]] = llvm.call spir_funccc @_Z27__spirv_GroupNonUniformIAddiij(%[[SCOPE]], %[[GROUPOP]], %[[ARG]]) {convergent, no_unwind, will_return} : (i32, i32, i32) -> i32
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniformArithmetic], []> {
  spirv.func @non_uniform_iadd_signed(%arg0: si32) -> si32 "None" {
    %0 = spirv.GroupNonUniformIAdd <Subgroup> <Reduce> %arg0 : si32 -> si32
    spirv.ReturnValue %0 : si32
  }
}

// -----

// Checks the `BitwiseAnd` variant (`WaveActiveBitAnd`'s own SPIR-V shape)
// also normalizes an unsigned `ui32` operand the same way.

// CHECK-LABEL: llvm.func spir_funccc @_Z33__spirv_GroupNonUniformBitwiseAndiij(i32, i32, i32) -> i32 attributes {convergent, no_unwind, will_return}
// CHECK-LABEL: llvm.func @non_uniform_bitwise_and_unsigned(
// CHECK-SAME:                                              %[[ARG:.*]]: i32) -> i32 {
// CHECK: %[[RESULT:.*]] = llvm.call spir_funccc @_Z33__spirv_GroupNonUniformBitwiseAndiij
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniformArithmetic], []> {
  spirv.func @non_uniform_bitwise_and_unsigned(%arg0: ui32) -> ui32 "None" {
    %0 = spirv.GroupNonUniformBitwiseAnd <Subgroup> <Reduce> %arg0 : ui32 -> ui32
    spirv.ReturnValue %0 : ui32
  }
}
