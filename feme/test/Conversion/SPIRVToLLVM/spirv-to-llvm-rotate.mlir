// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.GroupNonUniformRotateKHR` (roadmap F2,
// VK_KHR_shader_subgroup_rotate/shaderSubgroupRotate) -- the first
// `spirv.GroupNonUniform*` op this pass converts at all -- expands into the
// SPIR-V spec's own invocation-id arithmetic (`RotationGroupSize` is
// `SubgroupSize` when no `cluster_size` operand is given) followed by an
// `llvm.spv.wave.readlane` shuffle to that invocation.

// CHECK-LABEL: llvm.func @rotate
// CHECK: %[[LOCALID:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.local.invocation.id"() : () -> i32
// CHECK: %[[SIZE:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.size"() : () -> i32
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[MASK:.*]] = llvm.sub %[[SIZE]], %[[ONE]] : i32
// CHECK: %[[ALLONES:.*]] = llvm.mlir.constant(-1 : i32) : i32
// CHECK: %[[NOTMASK:.*]] = llvm.xor %[[MASK]], %[[ALLONES]] : i32
// CHECK: %[[ROTATED:.*]] = llvm.add %[[LOCALID]], %arg1 : i32
// CHECK: %[[ROTATEDMASKED:.*]] = llvm.and %[[ROTATED]], %[[MASK]] : i32
// CHECK: %[[BASEMASKED:.*]] = llvm.and %[[LOCALID]], %[[NOTMASK]] : i32
// CHECK: %[[TARGET:.*]] = llvm.add %[[ROTATEDMASKED]], %[[BASEMASKED]] : i32
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.readlane"(%arg0, %[[TARGET]]) : (f32, i32) -> f32
// CHECK: llvm.return %[[RESULT]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformRotateKHR], [SPV_KHR_subgroup_rotate]> {
  spirv.func @rotate(%value : f32, %delta : i32) -> f32 "None" {
    %0 = spirv.GroupNonUniformRotateKHR <Subgroup> %value, %delta : f32, i32 -> f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// Checks the `cluster_size` variant (roadmap F2,
// `shaderSubgroupRotateClustered`): `RotationGroupSize` becomes the operand
// itself instead of `llvm.spv.subgroup.size`, and the rest of the expansion
// is identical.

// CHECK-LABEL: llvm.func @clustered_rotate
// CHECK: %[[SIZE:.*]] = llvm.mlir.constant(4 : i32) : i32
// CHECK: %[[LOCALID:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.local.invocation.id"() : () -> i32
// CHECK-NOT: llvm.call_intrinsic "llvm.spv.subgroup.size"
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[MASK:.*]] = llvm.sub %[[SIZE]], %[[ONE]] : i32
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.readlane"
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformRotateKHR, GroupNonUniformClustered], [SPV_KHR_subgroup_rotate]> {
  spirv.func @clustered_rotate(%value : i32, %delta : i32) -> i32 "None" {
    %four = spirv.Constant 4 : i32
    %0 = spirv.GroupNonUniformRotateKHR <Subgroup> %value, %delta, cluster_size(%four) : i32, i32, i32 -> i32
    spirv.ReturnValue %0 : i32
  }
}
