// RUN: mlir-translate --no-implicit-module --test-spirv-roundtrip --split-input-file %s | FileCheck %s

// RUN: %if spirv-tools %{ rm -rf %t %}
// RUN: %if spirv-tools %{ mkdir %t %}
// RUN: %if spirv-tools %{ mlir-translate --no-implicit-module --serialize-spirv --split-input-file --spirv-save-validation-files-with-prefix=%t/module %s %}
// RUN: %if spirv-tools %{ spirv-val %t %}

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Geometry], []> {
  spirv.GlobalVariable @out : !spirv.ptr<!spirv.struct<(vector<4xf32>, f32, !spirv.array<1 x f32>)>, Output> 
  spirv.func @primitive_ops() "None" {
    // CHECK: spirv.EmitVertex
    spirv.EmitVertex
    // CHECK: spirv.EndPrimitive
    spirv.EndPrimitive
    spirv.Return
  }
  spirv.EntryPoint "Geometry" @primitive_ops, @out
  spirv.ExecutionMode @primitive_ops "InputPoints"
  spirv.ExecutionMode @primitive_ops "Invocations", 1
  spirv.ExecutionMode @primitive_ops "OutputLineStrip"
  spirv.ExecutionMode @primitive_ops "OutputVertices", 2
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [GeometryStreams], []> {
  spirv.GlobalVariable @out : !spirv.ptr<!spirv.struct<(vector<4xf32>, f32, !spirv.array<1 x f32>)>, Output>
  spirv.func @stream_primitive_ops() "None" {
    %0 = spirv.Constant 0 : i32
    // CHECK: spirv.EmitStreamVertex %{{.*}} : i32
    spirv.EmitStreamVertex %0 : i32
    // CHECK: spirv.EndStreamPrimitive %{{.*}} : i32
    spirv.EndStreamPrimitive %0 : i32
    spirv.Return
  }
  spirv.EntryPoint "Geometry" @stream_primitive_ops, @out
  spirv.ExecutionMode @stream_primitive_ops "InputPoints"
  spirv.ExecutionMode @stream_primitive_ops "Invocations", 1
  spirv.ExecutionMode @stream_primitive_ops "OutputLineStrip"
  spirv.ExecutionMode @stream_primitive_ops "OutputVertices", 2
}
