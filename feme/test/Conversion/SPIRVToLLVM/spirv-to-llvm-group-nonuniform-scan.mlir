// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks roadmap H126: `spirv.GroupNonUniformIAdd`/`FAdd`/`IMul`/`FMul`
// with an `ExclusiveScan` group operation (`WavePrefixSum`'s/
// `WavePrefixProduct`'s own SPIR-V shape) over a *signed* `si32` operand.
// Just like H124a's own `Reduce` case (see
// spirv-to-llvm-group-nonuniform-integer.mlir), upstream MLIR's own
// `GroupReducePattern` passes `si32`/`ui32` straight through to the
// `llvm.call`/`llvm.func` it builds -- which the LLVM dialect rejects
// outright (only a signless `i32` is a valid LLVM dialect type) -- so
// this pass's own higher-benefit `GroupNonUniformScanPattern` converts
// the result (and operand) through the type converter first, landing on
// a real `llvm.spv.wave.prefix.sum`/`.product` intrinsic call instead of
// a raw mangled builtin call, so `feme-cpu-simdize`'s own
// `WaveUniformity`/`SIMDize` passes (which only ever recognize a real
// `IntrinsicInst`) can classify and widen it correctly.

// CHECK-LABEL: llvm.func @non_uniform_iadd_exclusive_scan_signed(
// CHECK-SAME:                                                    %[[ARG:.*]]: i32) -> i32 {
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.prefix.sum"(%[[ARG]]) : (i32) -> i32
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniformArithmetic], []> {
  spirv.func @non_uniform_iadd_exclusive_scan_signed(%arg0: si32) -> si32 "None" {
    %0 = spirv.GroupNonUniformIAdd <Subgroup> <ExclusiveScan> %arg0 : si32 -> si32
    spirv.ReturnValue %0 : si32
  }
}

// -----

// Checks the floating-point `FAdd` variant lands on the same
// `llvm.spv.wave.prefix.sum` intrinsic.

// CHECK-LABEL: llvm.func @non_uniform_fadd_exclusive_scan(
// CHECK-SAME:                                             %[[ARG:.*]]: f32) -> f32 {
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.prefix.sum"(%[[ARG]]) : (f32) -> f32
// CHECK: llvm.return %[[RESULT]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniformArithmetic], []> {
  spirv.func @non_uniform_fadd_exclusive_scan(%arg0: f32) -> f32 "None" {
    %0 = spirv.GroupNonUniformFAdd <Subgroup> <ExclusiveScan> %arg0 : f32 -> f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// Checks the `IMul` variant (`WavePrefixProduct`'s own SPIR-V shape,
// unsigned operand) lands on `llvm.spv.wave.prefix.product`.

// CHECK-LABEL: llvm.func @non_uniform_imul_exclusive_scan_unsigned(
// CHECK-SAME:                                                      %[[ARG:.*]]: i32) -> i32 {
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.prefix.product"(%[[ARG]]) : (i32) -> i32
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniformArithmetic], []> {
  spirv.func @non_uniform_imul_exclusive_scan_unsigned(%arg0: ui32) -> ui32 "None" {
    %0 = spirv.GroupNonUniformIMul <Subgroup> <ExclusiveScan> %arg0 : ui32 -> ui32
    spirv.ReturnValue %0 : ui32
  }
}

// -----

// Checks the `FMul` variant lands on `llvm.spv.wave.prefix.product`.

// CHECK-LABEL: llvm.func @non_uniform_fmul_exclusive_scan(
// CHECK-SAME:                                             %[[ARG:.*]]: f32) -> f32 {
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.prefix.product"(%[[ARG]]) : (f32) -> f32
// CHECK: llvm.return %[[RESULT]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniformArithmetic], []> {
  spirv.func @non_uniform_fmul_exclusive_scan(%arg0: f32) -> f32 "None" {
    %0 = spirv.GroupNonUniformFMul <Subgroup> <ExclusiveScan> %arg0 : f32 -> f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// Checks that `InclusiveScan` (which has no HLSL-reachable intrinsic and
// no matching `GroupNonUniformScanPattern` support) still falls through
// to upstream's own generic `GroupReducePattern`/raw builtin call
// unchanged -- this pattern must not accidentally widen its match beyond
// `ExclusiveScan`.

// CHECK-LABEL: llvm.func @non_uniform_iadd_inclusive_scan(
// CHECK: llvm.call spir_funccc @_Z27__spirv_GroupNonUniformIAddiij(%{{.*}}) {{.*}} : (i32, i32, i32) -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniformArithmetic], []> {
  spirv.func @non_uniform_iadd_inclusive_scan(%arg0: i32) -> i32 "None" {
    %0 = spirv.GroupNonUniformIAdd <Subgroup> <InclusiveScan> %arg0 : i32 -> i32
    spirv.ReturnValue %0 : i32
  }
}
