// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.GroupNonUniformElect` (roadmap L7e) converts directly
// to `llvm.spv.wave.is_first_lane`, the same intrinsic this project's
// DXIL-origin frontend already produces for HLSL's `WaveIsFirstLane()`.

// CHECK-LABEL: llvm.func @elect
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.is_first_lane"() : () -> i1
// CHECK: llvm.return %[[RESULT]] : i1
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform], []> {
  spirv.func @elect() -> i1 "None" {
    %0 = spirv.GroupNonUniformElect <Subgroup> : i1
    spirv.ReturnValue %0 : i1
  }
}

// -----

// Checks the scalar variant of `spirv.GroupNonUniformAllEqual` (roadmap
// L7e), converting directly to `llvm.spv.wave.all_equal`.

// CHECK-LABEL: llvm.func @all_equal_scalar
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.all_equal"(%arg0) : (i32) -> i1
// CHECK: llvm.return %[[RESULT]] : i1
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformVote], []> {
  spirv.func @all_equal_scalar(%value : i32) -> i1 "None" {
    %0 = spirv.GroupNonUniformAllEqual <Subgroup> %value : i32, i1
    spirv.ReturnValue %0 : i1
  }
}

// -----

// Checks that `spirv.GroupNonUniformShuffle` (roadmap L7e) converts
// directly to `llvm.spv.wave.readlane`, exactly the same intrinsic
// `RotateConversionPattern` builds its own target invocation id for --
// unlike that pattern's derived id, `Id` here is passed straight through.

// CHECK-LABEL: llvm.func @shuffle
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.readlane"(%arg0, %arg1) : (f32, i32) -> f32
// CHECK: llvm.return %[[RESULT]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformShuffle], []> {
  spirv.func @shuffle(%value : f32, %id : i32) -> f32 "None" {
    %0 = spirv.GroupNonUniformShuffle <Subgroup> %value, %id : f32, i32
    spirv.ReturnValue %0 : f32
  }
}
