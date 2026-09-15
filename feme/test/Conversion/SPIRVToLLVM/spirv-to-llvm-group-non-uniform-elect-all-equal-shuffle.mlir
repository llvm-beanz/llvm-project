// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.GroupNonUniformElect` (roadmap L7e) converts directly
// to `llvm.spv.wave.is.first.lane`, the same intrinsic this project's
// DXIL-origin frontend already produces for HLSL's `WaveIsFirstLane()`.

// CHECK-LABEL: llvm.func @elect
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.is.first.lane"() : () -> i1
// CHECK: llvm.return %[[RESULT]] : i1
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform], []> {
  spirv.func @elect() -> i1 "None" {
    %0 = spirv.GroupNonUniformElect <Subgroup> : i1
    spirv.ReturnValue %0 : i1
  }
}

// -----

// Checks the scalar variant of `spirv.GroupNonUniformAllEqual` (roadmap
// L7e), converting directly to `llvm.spv.wave.all.equal`.

// CHECK-LABEL: llvm.func @all_equal_scalar
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.all.equal"(%arg0) : (i32) -> i1
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

// -----

// Checks that `spirv.GroupNonUniformAll`/`Any` (roadmap L7i) convert
// directly to `llvm.spv.wave.all`/`any`.

// CHECK-LABEL: llvm.func @vote_all
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.all"(%arg0) : (i1) -> i1
// CHECK: llvm.return %[[RESULT]] : i1
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformVote], []> {
  spirv.func @vote_all(%predicate : i1) -> i1 "None" {
    %0 = spirv.GroupNonUniformAll <Subgroup> %predicate : i1
    spirv.ReturnValue %0 : i1
  }
}

// -----

// CHECK-LABEL: llvm.func @vote_any
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.any"(%arg0) : (i1) -> i1
// CHECK: llvm.return %[[RESULT]] : i1
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformVote], []> {
  spirv.func @vote_any(%predicate : i1) -> i1 "None" {
    %0 = spirv.GroupNonUniformAny <Subgroup> %predicate : i1
    spirv.ReturnValue %0 : i1
  }
}

// -----

// Checks the *vector*-operand variant of `spirv.GroupNonUniformAllEqual`
// (roadmap L7i): the intrinsic call itself yields a per-component
// `vector<2xi1>` (matching `llvm.spv.wave.all.equal`'s own
// `LLVMScalarOrSameVectorWidth<0, i1>` shape), which then gets AND-reduced
// down to the single scalar `i1` this op's result type actually requires.

// CHECK-LABEL: llvm.func @all_equal_vector
// CHECK: %[[COMPONENTS:.*]] = llvm.call_intrinsic "llvm.spv.wave.all.equal"(%arg0) : (vector<2xi32>) -> vector<2xi1>
// CHECK: %[[RESULT:.*]] = "llvm.intr.vector.reduce.and"(%[[COMPONENTS]]) : (vector<2xi1>) -> i1
// CHECK: llvm.return %[[RESULT]] : i1
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformVote], []> {
  spirv.func @all_equal_vector(%value : vector<2xi32>) -> i1 "None" {
    %0 = spirv.GroupNonUniformAllEqual <Subgroup> %value : vector<2xi32>, i1
    spirv.ReturnValue %0 : i1
  }
}

// -----

// Checks that `spirv.GroupNonUniformShuffleXor` (roadmap L7i) converts to
// an `llvm.spv.subgroup.local.invocation.id`-derived xor'ed id fed into
// `llvm.spv.wave.readlane`, the same "compute an id, then shuffle" shape
// `RotateConversionPattern` (roadmap F2) already established.

// CHECK-LABEL: llvm.func @shuffle_xor
// CHECK: %[[ID:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.local.invocation.id"() : () -> i32
// CHECK: %[[TARGET:.*]] = llvm.xor %[[ID]], %arg1 : i32
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.readlane"(%arg0, %[[TARGET]]) : (f32, i32) -> f32
// CHECK: llvm.return %[[RESULT]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformShuffle], []> {
  spirv.func @shuffle_xor(%value : f32, %mask : i32) -> f32 "None" {
    %0 = spirv.GroupNonUniformShuffleXor <Subgroup> %value, %mask : f32, i32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// Checks that `spirv.GroupNonUniformQuadSwap` (roadmap H124l) converts to
// the same "compute a target id via `llvm.spv.subgroup.local.invocation.id`
// xor'ed with a mask, then `llvm.spv.wave.readlane`" shape `shuffle_xor`
// above uses, but with `Direction`'s own enum value plus one (here,
// `Horizontal` = 0, so mask = 1) as a compile-time-constant mask instead of
// a runtime operand.

// CHECK-LABEL: llvm.func @quad_swap_horizontal
// CHECK: %[[ID:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.local.invocation.id"() : () -> i32
// CHECK: %[[MASK:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[TARGET:.*]] = llvm.xor %[[ID]], %[[MASK]] : i32
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.readlane"(%arg0, %[[TARGET]]) : (f32, i32) -> f32
// CHECK: llvm.return %[[RESULT]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformQuad], []> {
  spirv.func @quad_swap_horizontal(%value : f32) -> f32 "None" {
    %0 = spirv.GroupNonUniformQuadSwap <Subgroup> <Horizontal> %value : f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// Checks the `Vertical` direction (mask = 2) and a vector operand.

// CHECK-LABEL: llvm.func @quad_swap_vertical_vector
// CHECK: %[[ID:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.local.invocation.id"() : () -> i32
// CHECK: %[[MASK:.*]] = llvm.mlir.constant(2 : i32) : i32
// CHECK: %[[TARGET:.*]] = llvm.xor %[[ID]], %[[MASK]] : i32
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.readlane"(%arg0, %[[TARGET]]) : (vector<4xf32>, i32) -> vector<4xf32>
// CHECK: llvm.return %[[RESULT]] : vector<4xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformQuad], []> {
  spirv.func @quad_swap_vertical_vector(%value : vector<4xf32>) -> vector<4xf32> "None" {
    %0 = spirv.GroupNonUniformQuadSwap <Subgroup> <Vertical> %value : vector<4xf32>
    spirv.ReturnValue %0 : vector<4xf32>
  }
}

// -----

// Checks the `Diagonal` direction (mask = 3) and an integer operand.

// CHECK-LABEL: llvm.func @quad_swap_diagonal
// CHECK: %[[ID:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.local.invocation.id"() : () -> i32
// CHECK: %[[MASK:.*]] = llvm.mlir.constant(3 : i32) : i32
// CHECK: %[[TARGET:.*]] = llvm.xor %[[ID]], %[[MASK]] : i32
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.wave.readlane"(%arg0, %[[TARGET]]) : (i32, i32) -> i32
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformQuad], []> {
  spirv.func @quad_swap_diagonal(%value : i32) -> i32 "None" {
    %0 = spirv.GroupNonUniformQuadSwap <Subgroup> <Diagonal> %value : i32
    spirv.ReturnValue %0 : i32
  }
}
