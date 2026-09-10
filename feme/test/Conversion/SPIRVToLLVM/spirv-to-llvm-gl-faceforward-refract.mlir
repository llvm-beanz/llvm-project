// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L87: `spirv.GL.FaceForward`/`spirv.GL.Refract` had no conversion
// pattern at all before this fix -- neither this file's own
// `populateSPIRVToLLVMTargetPatterns` nor upstream MLIR's own
// `populateSPIRVToLLVMConversionPatterns` registered one for either op
// (confirmed by grepping both), so a real dxc-compiled HLSL
// `faceforward()`/`refract()` call failed pipeline creation with "failed to
// legalize operation ... that was explicitly marked illegal" once L87's own
// deserialization-side fix (adding the two ops to the SPIR-V dialect itself)
// let them reach this stage for the first time.

// `FaceForward(N, I, Nref)` lowers to the GLSL.std.450 spec's own literal
// definition: `dot(Nref, I) < 0 ? N : -N`.
// CHECK-LABEL: llvm.func @faceforward_scalar
// CHECK: %[[DOT:.*]] = llvm.fmul %arg2, %arg1 : f32
// CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0.000000e+00 : f32) : f32
// CHECK: %[[CMP:.*]] = llvm.fcmp "olt" %[[DOT]], %[[ZERO]] : f32
// CHECK: %[[NEG:.*]] = llvm.fneg %arg0 : f32
// CHECK: %[[RES:.*]] = llvm.select %[[CMP]], %arg0, %[[NEG]] : i1, f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @faceforward_scalar(%n: f32, %i: f32, %nref: f32) -> (f32) "None" {
    %0 = spirv.GL.FaceForward %n, %i, %nref : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @faceforward_scalar
  spirv.ExecutionMode @faceforward_scalar "LocalSize", 1, 1, 1
}

// -----

// The vector case's dot product reduces to a scalar `i1` compare, which
// must then be broadcast (`llvm.insertelement` + zero-mask
// `llvm.shufflevector`) up to the full vector width before it can select
// between the two vector-shaped operands.
// CHECK-LABEL: llvm.func @faceforward_vector
// CHECK: %[[CMP:.*]] = llvm.fcmp "olt" %{{.*}}, %{{.*}} : f32
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : vector<3xi1>
// CHECK: %[[INS:.*]] = llvm.insertelement %[[CMP]], %[[POISON]][%{{.*}} : i32] : vector<3xi1>
// CHECK: %[[BCAST:.*]] = llvm.shufflevector %[[INS]], %[[POISON]] [0, 0, 0] : vector<3xi1>
// CHECK: %[[NEG:.*]] = llvm.fneg %arg0 : vector<3xf32>
// CHECK: %[[RES:.*]] = llvm.select %[[BCAST]], %arg0, %[[NEG]] : vector<3xi1>, vector<3xf32>
// CHECK: llvm.return %[[RES]] : vector<3xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @faceforward_vector(%n: vector<3xf32>, %i: vector<3xf32>, %nref: vector<3xf32>) -> (vector<3xf32>) "None" {
    %0 = spirv.GL.FaceForward %n, %i, %nref : vector<3xf32>
    spirv.ReturnValue %0 : vector<3xf32>
  }
  spirv.EntryPoint "GLCompute" @faceforward_vector
  spirv.ExecutionMode @faceforward_vector "LocalSize", 1, 1, 1
}

// -----

// `Refract(I, N, eta)` lowers to the GLSL.std.450 spec's own literal
// definition: `k = 1 - eta * eta * (1 - dot(N, I)^2)`, then `k < 0 ?
// genType(0) : eta * I - (eta * dot(N, I) + sqrt(k)) * N`. `eta` is always
// a scalar (per `spirv.GL.Refract`'s own op definition), even when `I`/`N`
// are vectors.
// CHECK-LABEL: llvm.func @refract_scalar
// CHECK: %[[DOT:.*]] = llvm.fmul %arg1, %arg0 : f32
// CHECK: %[[ETASQ:.*]] = llvm.fmul %arg2, %arg2 : f32
// CHECK: %[[DOTSQ:.*]] = llvm.fmul %[[DOT]], %[[DOT]] : f32
// CHECK-DAG: %[[ONE:.*]] = llvm.mlir.constant(1.000000e+00 : f32) : f32
// CHECK: %[[ONE_MINUS_DOTSQ:.*]] = llvm.fsub %[[ONE]], %[[DOTSQ]] : f32
// CHECK: %[[ETASQ_TIMES:.*]] = llvm.fmul %[[ETASQ]], %[[ONE_MINUS_DOTSQ]] : f32
// CHECK: %[[K:.*]] = llvm.fsub %[[ONE]], %[[ETASQ_TIMES]] : f32
// CHECK-DAG: %[[ZERO:.*]] = llvm.mlir.constant(0.000000e+00 : f32) : f32
// CHECK: %[[ISNEG:.*]] = llvm.fcmp "olt" %[[K]], %{{.*}} : f32
// CHECK: %[[ETAI:.*]] = llvm.fmul %arg2, %arg0 : f32
// CHECK: %[[SQRTK:.*]] = llvm.intr.sqrt(%[[K]]) : (f32) -> f32
// CHECK: %[[ETADOT:.*]] = llvm.fmul %arg2, %[[DOT]] : f32
// CHECK: %[[SUM:.*]] = llvm.fadd %[[ETADOT]], %[[SQRTK]] : f32
// CHECK: %[[SUM_TIMES_N:.*]] = llvm.fmul %[[SUM]], %arg1 : f32
// CHECK: %[[RESULT_IF_NONNEG:.*]] = llvm.fsub %[[ETAI]], %[[SUM_TIMES_N]] : f32
// CHECK: %[[RES:.*]] = llvm.select %[[ISNEG]], %[[ZERO]], %[[RESULT_IF_NONNEG]] : i1, f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @refract_scalar(%i: f32, %n: f32, %eta: f32) -> (f32) "None" {
    %0 = spirv.GL.Refract %i, %n, %eta : f32, f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @refract_scalar
  spirv.ExecutionMode @refract_scalar "LocalSize", 1, 1, 1
}

// -----

// The vector case broadcasts the always-scalar `eta` (and the scalar `k <
// 0` compare, and the scalar `eta * dot(N,I) + sqrt(k)` sum) up to the
// vector width via the same `llvm.insertelement` + zero-mask
// `llvm.shufflevector` idiom used above for `FaceForward`'s own scalar
// compare, wherever each needs to combine arithmetically with the
// vector-shaped `I`/`N`/result.
// CHECK-LABEL: llvm.func @refract_vector
// CHECK: %[[ONE_MINUS_DOTSQ:.*]] = llvm.fsub %{{.*}}, %{{.*}} : f32
// CHECK: %[[ETASQ_TIMES:.*]] = llvm.fmul %{{.*}}, %[[ONE_MINUS_DOTSQ]] : f32
// CHECK: %[[K:.*]] = llvm.fsub %{{.*}}, %[[ETASQ_TIMES]] : f32
// CHECK: %[[ZEROVEC:.*]] = llvm.mlir.constant(dense<0.000000e+00> : vector<3xf32>) : vector<3xf32>
// CHECK: %[[ISNEG:.*]] = llvm.fcmp "olt" %[[K]], %{{.*}} : f32
// CHECK: %[[POISON1:.*]] = llvm.mlir.poison : vector<3xi1>
// CHECK: %[[BCAST_ISNEG:.*]] = llvm.shufflevector %{{.*}}, %[[POISON1]] [0, 0, 0] : vector<3xi1>
// CHECK: %[[POISON2:.*]] = llvm.mlir.poison : vector<3xf32>
// CHECK: %[[BCAST_ETA:.*]] = llvm.shufflevector %{{.*}}, %[[POISON2]] [0, 0, 0] : vector<3xf32>
// CHECK: %[[ETAI:.*]] = llvm.fmul %[[BCAST_ETA]], %arg0 : vector<3xf32>
// CHECK: %[[SQRTK:.*]] = llvm.intr.sqrt(%[[K]]) : (f32) -> f32
// CHECK: %[[POISON3:.*]] = llvm.mlir.poison : vector<3xf32>
// CHECK: %[[BCAST_SUM:.*]] = llvm.shufflevector %{{.*}}, %[[POISON3]] [0, 0, 0] : vector<3xf32>
// CHECK: %[[SUM_TIMES_N:.*]] = llvm.fmul %[[BCAST_SUM]], %arg1 : vector<3xf32>
// CHECK: %[[RESULT_IF_NONNEG:.*]] = llvm.fsub %[[ETAI]], %[[SUM_TIMES_N]] : vector<3xf32>
// CHECK: %[[RES:.*]] = llvm.select %[[BCAST_ISNEG]], %[[ZEROVEC]], %[[RESULT_IF_NONNEG]] : vector<3xi1>, vector<3xf32>
// CHECK: llvm.return %[[RES]] : vector<3xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @refract_vector(%i: vector<3xf32>, %n: vector<3xf32>, %eta: f32) -> (vector<3xf32>) "None" {
    %0 = spirv.GL.Refract %i, %n, %eta : vector<3xf32>, f32
    spirv.ReturnValue %0 : vector<3xf32>
  }
  spirv.EntryPoint "GLCompute" @refract_vector
  spirv.ExecutionMode @refract_vector "LocalSize", 1, 1, 1
}
