// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L120: `spirv.GL.Ldexp` had a TableGen op definition (opcode 53)
// but no conversion pattern at all before this fix -- neither this
// file's own `populateSPIRVToLLVMTargetPatterns` nor upstream MLIR's own
// `populateSPIRVToLLVMConversionPatterns` registered one, so it failed
// pipeline creation with "failed to legalize operation ... that was
// explicitly marked illegal".

// The scalar form maps directly onto a single `llvm.intr.ldexp` call.
// CHECK-LABEL: llvm.func @ldexp_scalar
// CHECK: %[[RES:.*]] = llvm.intr.ldexp(%arg0, %arg1) : (f32, i32) -> f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @ldexp_scalar(%x: f32, %e: i32) -> (f32) "None" {
    %0 = spirv.GL.Ldexp %x : f32, %e : i32 -> f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @ldexp_scalar
  spirv.ExecutionMode @ldexp_scalar "LocalSize", 1, 1, 1
}

// -----

// The vector form needs one scalar `llvm.intr.ldexp` call per lane --
// `LLVM::LoadExpOp`'s own `power` operand is constrained to a scalar
// integer even when its `val` operand is a vector (unlike
// `spirv.GL.Ldexp`'s own `exp` operand, which is a full per-lane vector
// matching `x`'s component count), so a single call cannot cover the
// vector case.
// CHECK-LABEL: llvm.func @ldexp_vec
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : vector<3xf32>
// CHECK: %[[IDX0:.*]] = llvm.mlir.constant(0 : i64) : i64
// CHECK: %[[X0:.*]] = llvm.extractelement %arg0[%[[IDX0]] : i64] : vector<3xf32>
// CHECK: %[[E0:.*]] = llvm.extractelement %arg1[%[[IDX0]] : i64] : vector<3xi32>
// CHECK: %[[R0:.*]] = llvm.intr.ldexp(%[[X0]], %[[E0]]) : (f32, i32) -> f32
// CHECK: %[[V0:.*]] = llvm.insertelement %[[R0]], %[[POISON]][%[[IDX0]] : i64] : vector<3xf32>
// CHECK: %[[IDX1:.*]] = llvm.mlir.constant(1 : i64) : i64
// CHECK: %[[X1:.*]] = llvm.extractelement %arg0[%[[IDX1]] : i64] : vector<3xf32>
// CHECK: %[[E1:.*]] = llvm.extractelement %arg1[%[[IDX1]] : i64] : vector<3xi32>
// CHECK: %[[R1:.*]] = llvm.intr.ldexp(%[[X1]], %[[E1]]) : (f32, i32) -> f32
// CHECK: %[[V1:.*]] = llvm.insertelement %[[R1]], %[[V0]][%[[IDX1]] : i64] : vector<3xf32>
// CHECK: %[[IDX2:.*]] = llvm.mlir.constant(2 : i64) : i64
// CHECK: %[[X2:.*]] = llvm.extractelement %arg0[%[[IDX2]] : i64] : vector<3xf32>
// CHECK: %[[E2:.*]] = llvm.extractelement %arg1[%[[IDX2]] : i64] : vector<3xi32>
// CHECK: %[[R2:.*]] = llvm.intr.ldexp(%[[X2]], %[[E2]]) : (f32, i32) -> f32
// CHECK: %[[V2:.*]] = llvm.insertelement %[[R2]], %[[V1]][%[[IDX2]] : i64] : vector<3xf32>
// CHECK: llvm.return %[[V2]] : vector<3xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @ldexp_vec(%x: vector<3xf32>, %e: vector<3xi32>) -> (vector<3xf32>) "None" {
    %0 = spirv.GL.Ldexp %x : vector<3xf32>, %e : vector<3xi32> -> vector<3xf32>
    spirv.ReturnValue %0 : vector<3xf32>
  }
  spirv.EntryPoint "GLCompute" @ldexp_vec
  spirv.ExecutionMode @ldexp_vec "LocalSize", 1, 1, 1
}
