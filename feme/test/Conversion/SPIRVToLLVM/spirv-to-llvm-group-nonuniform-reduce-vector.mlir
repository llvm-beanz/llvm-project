// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks roadmap H124a: a *vector*-operand `spirv.GroupNonUniformFMax`
// (`WaveActiveMax(float3)`'s own SPIR-V shape) converts directly to the
// vector-typed overload of `llvm.spv.wave.reduce.max`, unchanged -- every
// `llvm.spv.wave.*` reduce intrinsic is itself overloaded on a vector
// operand type (`llvm/include/llvm/IR/IntrinsicsSPIRV.td`), and SPIR-V
// defines `OpGroupNonUniform*` arithmetic reductions component-wise, so no
// MLIR-level scalarization is needed at all (unlike this pattern's own
// short-lived predecessor, `VectorGroupNonUniformReducePattern`, which
// scalarized into one call per lane -- see `GroupNonUniformReducePattern`'s
// own comment in SPIRVToLLVMPatterns.cpp for why that approach never
// actually closed any real CTS case: it fell over one layer downstream, in
// `feme::cpu::SIMDizePass`, which -- unlike this intrinsic's own
// `llvm.spv.wave.*` shape -- never recognized the raw scalarized call it
// built).
// CHECK-LABEL: llvm.func @non_uniform_fmax_vector(
// CHECK-SAME:                                     %[[ARG:.*]]: vector<3xf32>) -> vector<3xf32> {
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.reduce.max"(%[[ARG]]) : (vector<3xf32>) -> vector<3xf32>
// CHECK: llvm.return %[[RESULT]] : vector<3xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniformArithmetic], []> {
  spirv.func @non_uniform_fmax_vector(%arg0: vector<3xf32>) -> vector<3xf32> "None" {
    %0 = spirv.GroupNonUniformFMax <Subgroup> <Reduce> %arg0 : vector<3xf32> -> vector<3xf32>
    spirv.ReturnValue %0 : vector<3xf32>
  }
}

// -----

// Checks the same vector normalization for a *signed integer* operand
// (`WaveActiveSum(int2)`'s own SPIR-V shape, `si32` fixed up to plain
// `i32` the same way `spirv-to-llvm-group-nonuniform-integer.mlir`'s own
// scalar case is).
// CHECK-LABEL: llvm.func @non_uniform_iadd_vector(
// CHECK-SAME:                                     %[[ARG:.*]]: vector<2xi32>) -> vector<2xi32> {
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.reduce.sum"(%[[ARG]]) : (vector<2xi32>) -> vector<2xi32>
// CHECK: llvm.return %[[RESULT]] : vector<2xi32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniformArithmetic], []> {
  spirv.func @non_uniform_iadd_vector(%arg0: vector<2xsi32>) -> vector<2xsi32> "None" {
    %0 = spirv.GroupNonUniformIAdd <Subgroup> <Reduce> %arg0 : vector<2xsi32> -> vector<2xsi32>
    spirv.ReturnValue %0 : vector<2xsi32>
  }
}

// -----

// Checks that a `ClusteredReduce` group operation -- which has no matching
// `llvm.spv.wave.*` intrinsic (every one takes a single operand, no
// cluster size) -- still falls through to upstream's own
// `GroupReducePattern`, unlike the ordinary whole-subgroup `Reduce` case
// above (not reachable from any HLSL `Wave*` intrinsic today, but
// upstream's own pattern must still keep handling it).
// CHECK-LABEL: llvm.func @non_uniform_fadd_clustered(
// CHECK: llvm.call spir_funccc @_Z27__spirv_GroupNonUniformFAddiifj
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniformArithmetic, GroupNonUniformClustered], []> {
  spirv.func @non_uniform_fadd_clustered(%arg0: f32) -> f32 "None" {
    %size = spirv.Constant 4 : i32
    %0 = spirv.GroupNonUniformFAdd <Subgroup> <ClusteredReduce> %arg0 cluster_size(%size) : f32, i32 -> f32
    spirv.ReturnValue %0 : f32
  }
}
