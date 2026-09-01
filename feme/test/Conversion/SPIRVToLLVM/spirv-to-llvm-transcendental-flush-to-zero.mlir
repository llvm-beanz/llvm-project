// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H6m: `spirv.GL.Log`/`Log2`/`Sqrt`/`Sinh`/`InverseSqrt`/`Radians`/
// `Degrees` each model a real GPU's own special-function hardware unit,
// which (unlike ordinary arithmetic) flushes a subnormal operand to a
// same-signed zero unconditionally -- regardless of whether the module
// declares `VK_KHR_shader_float_controls`'s `DenormFlushToZero` execution
// mode at all (`dxc` never emits it for a plain HLSL `log`/`sqrt`/etc.
// call, yet a real GPU's own reference output for a subnormal input still
// behaves as if it had been flushed first).

// CHECK-LABEL: llvm.func @log_flush
// CHECK: %[[SUBNORMAL:.*]] = "llvm.intr.is.fpclass"(%arg0) <{bit = 144 : i32}> : (f32) -> i1
// CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0.000000e+00 : f32) : f32
// CHECK: %[[SIGNED_ZERO:.*]] = llvm.intr.copysign(%[[ZERO]], %arg0) : (f32, f32) -> f32
// CHECK: %[[FLUSHED:.*]] = llvm.select %[[SUBNORMAL]], %[[SIGNED_ZERO]], %arg0 : i1, f32
// CHECK: %[[RES:.*]] = llvm.intr.log(%[[FLUSHED]]) : (f32) -> f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @log_flush(%a: f32) -> (f32) "None" {
    %0 = spirv.GL.Log %a : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @log_flush
  spirv.ExecutionMode @log_flush "LocalSize", 1, 1, 1
}

// -----

// CHECK-LABEL: llvm.func @log2_flush
// CHECK: llvm.select
// CHECK: %[[RES:.*]] = llvm.intr.log2(%{{.*}}) : (f32) -> f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @log2_flush(%a: f32) -> (f32) "None" {
    %0 = spirv.GL.Log2 %a : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @log2_flush
  spirv.ExecutionMode @log2_flush "LocalSize", 1, 1, 1
}

// -----

// CHECK-LABEL: llvm.func @sqrt_flush
// CHECK: llvm.select
// CHECK: %[[RES:.*]] = llvm.intr.sqrt(%{{.*}}) : (f32) -> f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @sqrt_flush(%a: f32) -> (f32) "None" {
    %0 = spirv.GL.Sqrt %a : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @sqrt_flush
  spirv.ExecutionMode @sqrt_flush "LocalSize", 1, 1, 1
}

// -----

// CHECK-LABEL: llvm.func @sinh_flush
// CHECK: llvm.select
// CHECK: %[[RES:.*]] = llvm.intr.sinh(%{{.*}}) : (f32) -> f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @sinh_flush(%a: f32) -> (f32) "None" {
    %0 = spirv.GL.Sinh %a : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @sinh_flush
  spirv.ExecutionMode @sinh_flush "LocalSize", 1, 1, 1
}

// -----

// `rsqrt` (HLSL) / `spirv.GL.InverseSqrt` lowers to `1.0 / llvm.sqrt(x)`,
// same as upstream's own `InverseSqrtPattern`, but with the operand
// flushed first.
// CHECK-LABEL: llvm.func @inverse_sqrt_flush
// CHECK: %[[SUBNORMAL:.*]] = "llvm.intr.is.fpclass"(%arg0)
// CHECK: %[[FLUSHED:.*]] = llvm.select %[[SUBNORMAL]], %{{.*}}, %arg0 : i1, f32
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1.000000e+00 : f32) : f32
// CHECK: %[[SQRT:.*]] = llvm.intr.sqrt(%[[FLUSHED]]) : (f32) -> f32
// CHECK: %[[RES:.*]] = llvm.fdiv %[[ONE]], %[[SQRT]] : f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @inverse_sqrt_flush(%a: f32) -> (f32) "None" {
    %0 = spirv.GL.InverseSqrt %a : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @inverse_sqrt_flush
  spirv.ExecutionMode @inverse_sqrt_flush "LocalSize", 1, 1, 1
}

// -----

// `radians`/`degrees` (HLSL) / `spirv.GL.Radians`/`spirv.GL.Degrees` lower
// to a plain multiply by a compile-time constant, same as upstream's own
// `ScalePattern`, but with the operand flushed first: unlike an ordinary
// `spirv.FMul` by a non-unit constant, scaling a subnormal keeps the
// result subnormal-scale (nonzero) under real, denormal-preserving
// IEEE-754 math, while a real GPU's own reference answer is a same-signed
// zero, as if flushed first.
// CHECK-LABEL: llvm.func @radians_flush
// CHECK: %[[SUBNORMAL:.*]] = "llvm.intr.is.fpclass"(%arg0)
// CHECK: %[[FLUSHED:.*]] = llvm.select %[[SUBNORMAL]], %{{.*}}, %arg0 : i1, f32
// CHECK: %[[FACTOR:.*]] = llvm.mlir.constant(0.0174532924 : f32) : f32
// CHECK: %[[RES:.*]] = llvm.fmul %[[FLUSHED]], %[[FACTOR]] : f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @radians_flush(%a: f32) -> (f32) "None" {
    %0 = spirv.GL.Radians %a : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @radians_flush
  spirv.ExecutionMode @radians_flush "LocalSize", 1, 1, 1
}

// -----

// CHECK-LABEL: llvm.func @degrees_flush
// CHECK: %[[SUBNORMAL:.*]] = "llvm.intr.is.fpclass"(%arg0)
// CHECK: %[[FLUSHED:.*]] = llvm.select %[[SUBNORMAL]], %{{.*}}, %arg0 : i1, f32
// CHECK: %[[FACTOR:.*]] = llvm.mlir.constant(57.2957802 : f32) : f32
// CHECK: %[[RES:.*]] = llvm.fmul %[[FLUSHED]], %[[FACTOR]] : f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @degrees_flush(%a: f32) -> (f32) "None" {
    %0 = spirv.GL.Degrees %a : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.EntryPoint "GLCompute" @degrees_flush
  spirv.ExecutionMode @degrees_flush "LocalSize", 1, 1, 1
}

// -----

// A vector-typed operand flushes lane-wise, same as
// `spirv-to-llvm-denorm-flush-to-zero.mlir`'s own vector coverage for
// ordinary arithmetic.
// CHECK-LABEL: llvm.func @sqrt_flush_vector
// CHECK: %[[SUBNORMAL:.*]] = "llvm.intr.is.fpclass"(%arg0) <{bit = 144 : i32}> : (vector<4xf32>) -> vector<4xi1>
// CHECK: %[[FLUSHED:.*]] = llvm.select %[[SUBNORMAL]], %{{.*}}, %arg0 : vector<4xi1>, vector<4xf32>
// CHECK: %[[RES:.*]] = llvm.intr.sqrt(%[[FLUSHED]]) : (vector<4xf32>) -> vector<4xf32>
// CHECK: llvm.return %[[RES]] : vector<4xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @sqrt_flush_vector(%a: vector<4xf32>) -> (vector<4xf32>) "None" {
    %0 = spirv.GL.Sqrt %a : vector<4xf32>
    spirv.ReturnValue %0 : vector<4xf32>
  }
  spirv.EntryPoint "GLCompute" @sqrt_flush_vector
  spirv.ExecutionMode @sqrt_flush_vector "LocalSize", 1, 1, 1
}
