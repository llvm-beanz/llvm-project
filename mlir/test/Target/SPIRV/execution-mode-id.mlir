// RUN: mlir-translate --no-implicit-module --split-input-file --test-spirv-roundtrip %s | FileCheck %s

// RUN: %if spirv-tools %{ rm -rf %t %}
// RUN: %if spirv-tools %{ mkdir %t %}
// RUN: %if spirv-tools %{ mlir-translate --no-implicit-module --serialize-spirv --split-input-file --spirv-save-validation-files-with-prefix=%t/module %s %}
// RUN: %if spirv-tools %{ spirv-val %t %}

spirv.module Logical GLSL450 requires #spirv.vce<v1.2, [Shader], []> {
  spirv.SpecConstant @x = 3 : i32
  spirv.SpecConstant @y = 4 : i32
  spirv.SpecConstant @z = 5 : i32
  spirv.func @foo() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @foo
  // CHECK: spirv.ExecutionModeId @foo "LocalSizeId" @x, @y, @z
  spirv.ExecutionModeId @foo "LocalSizeId" @x, @y, @z
}

// -----

// `FPFastMathDefault` (roadmap F15d): unlike every other `ExecutionModeId`
// this file exercises, its two operands are a target type and a literal
// fast-math mode, not symbol references (see `ExecutionModeIdOp`'s own
// class comment) -- one `OpExecutionModeId` per floating-point type the
// entry point declares a default for.
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader, FloatControls2], [SPV_KHR_float_controls2]> {
  spirv.func @foo() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @foo
  // CHECK: spirv.ExecutionModeId @foo "FPFastMathDefault" f16, 4
  // CHECK: spirv.ExecutionModeId @foo "FPFastMathDefault" f32, 65536
  spirv.ExecutionModeId @foo "FPFastMathDefault" f16, 4
  spirv.ExecutionModeId @foo "FPFastMathDefault" f32, 65536
}

