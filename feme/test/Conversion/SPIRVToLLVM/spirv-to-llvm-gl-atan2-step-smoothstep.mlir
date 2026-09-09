// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L7c: `spirv.GL.Atan2`/`spirv.GL.Step`/`spirv.GL.SmoothStep` had no
// conversion pattern at all before this fix -- neither this file's own
// `populateSPIRVToLLVMTargetPatterns` nor upstream MLIR's own
// `populateSPIRVToLLVMConversionPatterns` registered one for any of the
// three, so every HLSL-derived shader reaching one of them failed pipeline
// creation with "failed to legalize operation ... that was explicitly
// marked illegal" (confirmed by hand-authoring each of this file's own
// cases against a pre-fix build). None of these three has ever been hit by
// a recorded `deqp-vk` case, per roadmap L7's own filing text -- this file
// exists purely to give the fix its own real, phase-scoped regression
// coverage.

// `Atan2` needs no arithmetic decomposition: `llvm.intr.atan2` is already a
// direct, component-wise equivalent, mirroring upstream's own
// `spirv.CL.atan2` -> `llvm.intr.atan2` mapping for the OpenCL sibling of
// this same GLSL.std.450 op.
// CHECK-LABEL: llvm.func @atan2_scalar
// CHECK: %[[RES:.*]] = llvm.intr.atan2(%arg0, %arg1) : (f32, f32) -> f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @atan2_scalar(%y: f32, %x: f32) -> (f32) "None" {
    %0 = spirv.GL.Atan2 %y, %x : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @atan2_scalar
  spirv.ExecutionMode @atan2_scalar "LocalSize", 1, 1, 1
}

// -----

// CHECK-LABEL: llvm.func @atan2_vector
// CHECK: %[[RES:.*]] = llvm.intr.atan2(%arg0, %arg1) : (vector<3xf32>, vector<3xf32>) -> vector<3xf32>
// CHECK: llvm.return %[[RES]] : vector<3xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @atan2_vector(%y: vector<3xf32>, %x: vector<3xf32>) -> (vector<3xf32>) "None" {
    %0 = spirv.GL.Atan2 %y, %x : vector<3xf32>
    spirv.ReturnValue %0 : vector<3xf32>
  }
  spirv.EntryPoint "GLCompute" @atan2_vector
  spirv.ExecutionMode @atan2_vector "LocalSize", 1, 1, 1
}

// -----

// `Step(edge, x)` lowers to `x < edge ? 0.0 : 1.0`, per the GLSL.std.450
// spec's own literal definition.
// CHECK-LABEL: llvm.func @step_scalar
// CHECK: %[[CMP:.*]] = llvm.fcmp "olt" %arg1, %arg0 : f32
// CHECK-DAG: %[[ZERO:.*]] = llvm.mlir.constant(0.000000e+00 : f32) : f32
// CHECK-DAG: %[[ONE:.*]] = llvm.mlir.constant(1.000000e+00 : f32) : f32
// CHECK: %[[RES:.*]] = llvm.select %[[CMP]], %[[ZERO]], %[[ONE]] : i1, f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @step_scalar(%edge: f32, %x: f32) -> (f32) "None" {
    %0 = spirv.GL.Step %edge, %x : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @step_scalar
  spirv.ExecutionMode @step_scalar "LocalSize", 1, 1, 1
}

// -----

// CHECK-LABEL: llvm.func @step_vector
// CHECK: %[[CMP:.*]] = llvm.fcmp "olt" %arg1, %arg0 : vector<2xf32>
// CHECK: %[[RES:.*]] = llvm.select %[[CMP]], %{{.*}}, %{{.*}} : vector<2xi1>, vector<2xf32>
// CHECK: llvm.return %[[RES]] : vector<2xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @step_vector(%edge: vector<2xf32>, %x: vector<2xf32>) -> (vector<2xf32>) "None" {
    %0 = spirv.GL.Step %edge, %x : vector<2xf32>
    spirv.ReturnValue %0 : vector<2xf32>
  }
  spirv.EntryPoint "GLCompute" @step_vector
  spirv.ExecutionMode @step_vector "LocalSize", 1, 1, 1
}

// -----

// `SmoothStep(edge0, edge1, x)` lowers to the GLSL.std.450 spec's own
// literal definition: `t = clamp((x - edge0) / (edge1 - edge0), 0, 1)`
// followed by `t * t * (3 - 2 * t)`, using `llvm.intr.maxnum`/`minnum` for
// the clamp (the same pair `spirv.GL.FMax`/`FMin` already map to).
// CHECK-LABEL: llvm.func @smoothstep_scalar
// CHECK: %[[NUM:.*]] = llvm.fsub %arg2, %arg0 : f32
// CHECK: %[[DEN:.*]] = llvm.fsub %arg1, %arg0 : f32
// CHECK: %[[RATIO:.*]] = llvm.fdiv %[[NUM]], %[[DEN]] : f32
// CHECK: %[[CLAMPED_LOW:.*]] = llvm.intr.maxnum(%[[RATIO]], %{{.*}}) : (f32, f32) -> f32
// CHECK: %[[T:.*]] = llvm.intr.minnum(%[[CLAMPED_LOW]], %{{.*}}) : (f32, f32) -> f32
// CHECK: %[[TWO_T:.*]] = llvm.fmul %{{.*}}, %[[T]] : f32
// CHECK: %[[THREE_MINUS_TWO_T:.*]] = llvm.fsub %{{.*}}, %[[TWO_T]] : f32
// CHECK: %[[T_SQUARED:.*]] = llvm.fmul %[[T]], %[[T]] : f32
// CHECK: %[[RES:.*]] = llvm.fmul %[[T_SQUARED]], %[[THREE_MINUS_TWO_T]] : f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @smoothstep_scalar(%edge0: f32, %edge1: f32, %x: f32) -> (f32) "None" {
    %0 = spirv.GL.SmoothStep %edge0, %edge1, %x : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @smoothstep_scalar
  spirv.ExecutionMode @smoothstep_scalar "LocalSize", 1, 1, 1
}

// -----

// CHECK-LABEL: llvm.func @smoothstep_vector
// CHECK: %[[T:.*]] = llvm.intr.minnum(%{{.*}}, %{{.*}}) : (vector<4xf32>, vector<4xf32>) -> vector<4xf32>
// CHECK: %[[TWO_T:.*]] = llvm.fmul %{{.*}}, %[[T]] : vector<4xf32>
// CHECK: %[[THREE_MINUS_TWO_T:.*]] = llvm.fsub %{{.*}}, %[[TWO_T]] : vector<4xf32>
// CHECK: %[[T_SQUARED:.*]] = llvm.fmul %[[T]], %[[T]] : vector<4xf32>
// CHECK: %[[RES:.*]] = llvm.fmul %[[T_SQUARED]], %[[THREE_MINUS_TWO_T]] : vector<4xf32>
// CHECK: llvm.return %[[RES]] : vector<4xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @smoothstep_vector(%edge0: vector<4xf32>, %edge1: vector<4xf32>, %x: vector<4xf32>) -> (vector<4xf32>) "None" {
    %0 = spirv.GL.SmoothStep %edge0, %edge1, %x : vector<4xf32>
    spirv.ReturnValue %0 : vector<4xf32>
  }
  spirv.EntryPoint "GLCompute" @smoothstep_vector
  spirv.ExecutionMode @smoothstep_vector "LocalSize", 1, 1, 1
}
