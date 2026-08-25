// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.KHR.AssumeTrue` (roadmap F4,
// VK_KHR_shader_expect_assume/shaderExpectAssume) converts directly to the
// `llvm.assume` intrinsic: both take a single `i1` condition and produce no
// result.

// CHECK-LABEL: llvm.func @assume
// CHECK: llvm.intr.assume %arg0 : i1
// CHECK: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, ExpectAssumeKHR], [SPV_KHR_expect_assume]> {
  spirv.func @assume(%cond : i1) -> () "None" {
    spirv.KHR.AssumeTrue %cond
    spirv.Return
  }
}

// -----

// Checks that `spirv.KHR.Expect` (roadmap F4, same extension as
// `spirv.KHR.AssumeTrue` above) converts directly to the `llvm.expect`
// intrinsic on a scalar integer operand.

// CHECK-LABEL: llvm.func @expect
// CHECK: %[[RESULT:.*]] = llvm.intr.expect %arg0, %arg1 : i32
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, ExpectAssumeKHR], [SPV_KHR_expect_assume]> {
  spirv.func @expect(%value : i32, %expected : i32) -> i32 "None" {
    %0 = spirv.KHR.Expect %value, %expected : i32
    spirv.ReturnValue %0 : i32
  }
}

// -----

// Checks the vector form of `spirv.KHR.Expect`: LLVM's `llvm.expect`
// intrinsic is scalar-only, so this expands into one `llvm.expect` call per
// lane, reassembled into the result vector.

// CHECK-LABEL: llvm.func @expect_vector
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : vector<2xi32>
// CHECK: %[[IDX0:.*]] = llvm.mlir.constant(0 : i64) : i64
// CHECK: %[[VAL0:.*]] = llvm.extractelement %arg0[%[[IDX0]] : i64] : vector<2xi32>
// CHECK: %[[EXP0:.*]] = llvm.extractelement %arg1[%[[IDX0]] : i64] : vector<2xi32>
// CHECK: %[[LANE0:.*]] = llvm.intr.expect %[[VAL0]], %[[EXP0]] : i32
// CHECK: %[[VEC0:.*]] = llvm.insertelement %[[LANE0]], %[[POISON]][%[[IDX0]] : i64] : vector<2xi32>
// CHECK: %[[IDX1:.*]] = llvm.mlir.constant(1 : i64) : i64
// CHECK: %[[VAL1:.*]] = llvm.extractelement %arg0[%[[IDX1]] : i64] : vector<2xi32>
// CHECK: %[[EXP1:.*]] = llvm.extractelement %arg1[%[[IDX1]] : i64] : vector<2xi32>
// CHECK: %[[LANE1:.*]] = llvm.intr.expect %[[VAL1]], %[[EXP1]] : i32
// CHECK: %[[VEC1:.*]] = llvm.insertelement %[[LANE1]], %[[VEC0]][%[[IDX1]] : i64] : vector<2xi32>
// CHECK: llvm.return %[[VEC1]] : vector<2xi32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, ExpectAssumeKHR], [SPV_KHR_expect_assume]> {
  spirv.func @expect_vector(%value : vector<2xi32>, %expected : vector<2xi32>) -> vector<2xi32> "None" {
    %0 = spirv.KHR.Expect %value, %expected : vector<2xi32>
    spirv.ReturnValue %0 : vector<2xi32>
  }
}
