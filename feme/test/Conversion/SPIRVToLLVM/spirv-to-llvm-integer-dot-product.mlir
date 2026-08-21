// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap E8 (`VK_KHR_shader_integer_dot_product`): `spirv.SDot`/`spirv.UDot`/
// `spirv.SUDot`/`spirv.SDotAccSat`/`spirv.SUDotAccSat`/`spirv.UDotAccSat`
// have no upstream MLIR conversion pattern at all, exactly like `spirv.Dot`
// (see spirv-to-llvm-dot.mlir). Each lowers to a per-lane sign/zero-extend,
// multiply, and add chain -- the integer analogue of `spirv.Dot`'s
// `llvm.intr.fmuladd` chain -- with a final saturating add for the
// `*AccSat` variants.

// CHECK-LABEL: llvm.func @sdot_vector
// CHECK: %[[A0:.*]] = llvm.extractelement %arg0[%{{.*}} : i64] : vector<4xi8>
// CHECK: %[[SA0:.*]] = llvm.sext %[[A0]] : i8 to i32
// CHECK: %[[B0:.*]] = llvm.extractelement %arg1[%{{.*}} : i64] : vector<4xi8>
// CHECK: %[[SB0:.*]] = llvm.sext %[[B0]] : i8 to i32
// CHECK: %[[A1:.*]] = llvm.extractelement %arg0[%{{.*}} : i64] : vector<4xi8>
// CHECK: %[[SA1:.*]] = llvm.sext %[[A1]] : i8 to i32
// CHECK: %[[B1:.*]] = llvm.extractelement %arg1[%{{.*}} : i64] : vector<4xi8>
// CHECK: %[[SB1:.*]] = llvm.sext %[[B1]] : i8 to i32
// CHECK: %[[MUL0:.*]] = llvm.mul %[[SA0]], %[[SB0]] : i32
// CHECK: %[[MUL1:.*]] = llvm.mul %[[SA1]], %[[SB1]] : i32
// CHECK: %[[SUM1:.*]] = llvm.add %[[MUL0]], %[[MUL1]] : i32
// CHECK: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @sdot_vector(%a : vector<4xi8>, %b : vector<4xi8>) -> i32 "None" {
    %0 = spirv.SDot %a, %b : vector<4xi8> -> i32
    spirv.ReturnValue %0 : i32
  }
}

// -----

// A scalar 32-bit operand is only legal together with the
// `PackedVectorFormat4x8Bit` format, and unpacks into its four constituent
// bytes, byte 0 in the low-order bits; `spirv.UDot` zero-extends both.

// CHECK-LABEL: llvm.func @udot_packed
// CHECK: %[[SHIFT0:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[SHIFTED_A0:.*]] = llvm.lshr %arg0, %[[SHIFT0]] : i32
// CHECK: %[[BYTE_A0:.*]] = llvm.trunc %[[SHIFTED_A0]] : i32 to i8
// CHECK: %[[UA0:.*]] = llvm.zext %[[BYTE_A0]] : i8 to i32
// CHECK: %[[SHIFT0B:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[SHIFTED_B0:.*]] = llvm.lshr %arg1, %[[SHIFT0B]] : i32
// CHECK: %[[BYTE_B0:.*]] = llvm.trunc %[[SHIFTED_B0]] : i32 to i8
// CHECK: %[[UB0:.*]] = llvm.zext %[[BYTE_B0]] : i8 to i32
// CHECK: %[[SHIFT1:.*]] = llvm.mlir.constant(8 : i32) : i32
// CHECK: llvm.lshr %arg0, %[[SHIFT1]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @udot_packed(%a : i32, %b : i32) -> i32 "None" {
    %0 = spirv.UDot %a, %b, <PackedVectorFormat4x8Bit> : i32 -> i32
    spirv.ReturnValue %0 : i32
  }
}

// -----

// `spirv.SUDot`: vector 1 is sign-extended, vector 2 is zero-extended.

// CHECK-LABEL: llvm.func @sudot_vector
// CHECK: %[[A0:.*]] = llvm.extractelement %arg0[%{{.*}} : i64] : vector<2xi8>
// CHECK: %[[SA0:.*]] = llvm.sext %[[A0]] : i8 to i32
// CHECK: %[[B0:.*]] = llvm.extractelement %arg1[%{{.*}} : i64] : vector<2xi8>
// CHECK: %[[UB0:.*]] = llvm.zext %[[B0]] : i8 to i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @sudot_vector(%a : vector<2xi8>, %b : vector<2xi8>) -> i32 "None" {
    %0 = spirv.SUDot %a, %b : vector<2xi8> -> i32
    spirv.ReturnValue %0 : i32
  }
}

// -----

// `spirv.SDotAccSat` adds a final signed saturating add of the accumulator.

// CHECK-LABEL: llvm.func @sdot_acc_sat
// CHECK: %[[SAT:.*]] = llvm.intr.sadd.sat(%{{.*}}, %arg2) : (i32, i32) -> i32
// CHECK: llvm.return %[[SAT]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @sdot_acc_sat(%a : vector<4xi8>, %b : vector<4xi8>, %acc : i32) -> i32 "None" {
    %0 = spirv.SDotAccSat %a, %b, %acc : vector<4xi8> -> i32
    spirv.ReturnValue %0 : i32
  }
}

// -----

// `spirv.UDotAccSat` uses an unsigned saturating add instead.

// CHECK-LABEL: llvm.func @udot_acc_sat
// CHECK: %[[SAT:.*]] = llvm.intr.uadd.sat(%{{.*}}, %arg2) : (i32, i32) -> i32
// CHECK: llvm.return %[[SAT]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @udot_acc_sat(%a : vector<4xi8>, %b : vector<4xi8>, %acc : i32) -> i32 "None" {
    %0 = spirv.UDotAccSat %a, %b, %acc : vector<4xi8> -> i32
    spirv.ReturnValue %0 : i32
  }
}

// -----

// `spirv.SUDotAccSat`: mixed signedness lanes, signed saturating add.

// CHECK-LABEL: llvm.func @sudot_acc_sat
// CHECK: %[[SAT:.*]] = llvm.intr.sadd.sat(%{{.*}}, %arg2) : (i32, i32) -> i32
// CHECK: llvm.return %[[SAT]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @sudot_acc_sat(%a : vector<4xi8>, %b : vector<4xi8>, %acc : i32) -> i32 "None" {
    %0 = spirv.SUDotAccSat %a, %b, %acc : vector<4xi8> -> i32
    spirv.ReturnValue %0 : i32
  }
}
