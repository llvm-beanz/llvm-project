// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H124f: `spirv.IsNan`/`spirv.IsInf`/`spirv.GL.Normalize`/
// `spirv.GL.Length` had no conversion pattern at all before this fix --
// neither this file's own `populateSPIRVToLLVMTargetPatterns` nor upstream
// MLIR's own `populateSPIRVToLLVMConversionPatterns` registered one for any
// of the four, so every HLSL-derived shape reaching one of them (e.g.
// `isnan`/`isinf`/`normalize`/`length` HLSL intrinsics) failed pipeline
// creation with "failed to legalize operation ... that was explicitly
// marked illegal".

// `IsNan(x)` lowers to `llvm.fcmp uno %x, %x`: a value is unordered with
// itself exactly when it's NaN.
// CHECK-LABEL: llvm.func @isnan_scalar
// CHECK: %[[RES:.*]] = llvm.fcmp "uno" %arg0, %arg0 : f32
// CHECK: llvm.return %[[RES]] : i1
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @isnan_scalar(%x: f32) -> (i1) "None" {
    %0 = spirv.IsNan %x : f32
    spirv.ReturnValue %0 : i1
  }
  spirv.EntryPoint "GLCompute" @isnan_scalar
  spirv.ExecutionMode @isnan_scalar "LocalSize", 1, 1, 1
}

// -----

// CHECK-LABEL: llvm.func @isnan_vector
// CHECK: %[[RES:.*]] = llvm.fcmp "uno" %arg0, %arg0 : vector<3xf32>
// CHECK: llvm.return %[[RES]] : vector<3xi1>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @isnan_vector(%x: vector<3xf32>) -> (vector<3xi1>) "None" {
    %0 = spirv.IsNan %x : vector<3xf32>
    spirv.ReturnValue %0 : vector<3xi1>
  }
  spirv.EntryPoint "GLCompute" @isnan_vector
  spirv.ExecutionMode @isnan_vector "LocalSize", 1, 1, 1
}

// -----

// `IsInf(x)` lowers to `llvm.fabs(x) == +Inf` (`llvm.fcmp oeq`), folding
// the sign away first so both infinity signs are caught by one compare.
// CHECK-LABEL: llvm.func @isinf_scalar
// CHECK: %[[ABS:.*]] = llvm.intr.fabs(%arg0) : (f32) -> f32
// CHECK: %[[INF:.*]] = llvm.mlir.constant(0x7F800000 : f32) : f32
// CHECK: %[[RES:.*]] = llvm.fcmp "oeq" %[[ABS]], %[[INF]] : f32
// CHECK: llvm.return %[[RES]] : i1
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @isinf_scalar(%x: f32) -> (i1) "None" {
    %0 = spirv.IsInf %x : f32
    spirv.ReturnValue %0 : i1
  }
  spirv.EntryPoint "GLCompute" @isinf_scalar
  spirv.ExecutionMode @isinf_scalar "LocalSize", 1, 1, 1
}

// -----

// CHECK-LABEL: llvm.func @isinf_vector
// CHECK: %[[ABS:.*]] = llvm.intr.fabs(%arg0) : (vector<2xf32>) -> vector<2xf32>
// CHECK: %[[RES:.*]] = llvm.fcmp "oeq" %[[ABS]], %{{.*}} : vector<2xf32>
// CHECK: llvm.return %[[RES]] : vector<2xi1>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @isinf_vector(%x: vector<2xf32>) -> (vector<2xi1>) "None" {
    %0 = spirv.IsInf %x : vector<2xf32>
    spirv.ReturnValue %0 : vector<2xi1>
  }
  spirv.EntryPoint "GLCompute" @isinf_vector
  spirv.ExecutionMode @isinf_vector "LocalSize", 1, 1, 1
}

// -----

// `Length(x)` lowers to `sqrt(dot(x, x))`, always producing a scalar
// result even for a vector operand.
// CHECK-LABEL: llvm.func @length_scalar
// CHECK: %[[DOT:.*]] = llvm.fmul %arg0, %arg0 : f32
// CHECK: %[[RES:.*]] = llvm.intr.sqrt(%[[DOT]]) : (f32) -> f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @length_scalar(%x: f32) -> (f32) "None" {
    %0 = spirv.GL.Length %x : f32 -> f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @length_scalar
  spirv.ExecutionMode @length_scalar "LocalSize", 1, 1, 1
}

// -----

// CHECK-LABEL: llvm.func @length_vector
// CHECK: %[[LANE00:.*]] = llvm.extractelement %arg0{{.*}} : vector<3xf32>
// CHECK: %[[LANE01:.*]] = llvm.extractelement %arg0{{.*}} : vector<3xf32>
// CHECK: llvm.fmul %[[LANE00]], %[[LANE01]] : f32
// CHECK: llvm.intr.fmuladd(%{{.*}}, %{{.*}}, %{{.*}}) : (f32, f32, f32) -> f32
// CHECK: %[[DOT:.*]] = llvm.intr.fmuladd(%{{.*}}, %{{.*}}, %{{.*}}) : (f32, f32, f32) -> f32
// CHECK: %[[RES:.*]] = llvm.intr.sqrt(%[[DOT]]) : (f32) -> f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @length_vector(%x: vector<3xf32>) -> (f32) "None" {
    %0 = spirv.GL.Length %x : vector<3xf32> -> f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @length_vector
  spirv.ExecutionMode @length_vector "LocalSize", 1, 1, 1
}

// -----

// `Normalize(x)` lowers to `x / Length(x)`, broadcasting the scalar length
// back to `x`'s own shape before dividing.
// CHECK-LABEL: llvm.func @normalize_scalar
// CHECK: %[[DOT:.*]] = llvm.fmul %arg0, %arg0 : f32
// CHECK: %[[LEN:.*]] = llvm.intr.sqrt(%[[DOT]]) : (f32) -> f32
// CHECK: %[[RES:.*]] = llvm.fdiv %arg0, %[[LEN]] : f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @normalize_scalar(%x: f32) -> (f32) "None" {
    %0 = spirv.GL.Normalize %x : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @normalize_scalar
  spirv.ExecutionMode @normalize_scalar "LocalSize", 1, 1, 1
}

// -----

// CHECK-LABEL: llvm.func @normalize_vector
// CHECK: %[[LEN:.*]] = llvm.intr.sqrt(%{{.*}}) : (f32) -> f32
// CHECK: %[[BROADCAST:.*]] = llvm.shufflevector %{{.*}}, %{{.*}} [0, 0, 0, 0] : vector<4xf32>
// CHECK: %[[RES:.*]] = llvm.fdiv %arg0, %[[BROADCAST]] : vector<4xf32>
// CHECK: llvm.return %[[RES]] : vector<4xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @normalize_vector(%x: vector<4xf32>) -> (vector<4xf32>) "None" {
    %0 = spirv.GL.Normalize %x : vector<4xf32>
    spirv.ReturnValue %0 : vector<4xf32>
  }
  spirv.EntryPoint "GLCompute" @normalize_vector
  spirv.ExecutionMode @normalize_vector "LocalSize", 1, 1, 1
}
