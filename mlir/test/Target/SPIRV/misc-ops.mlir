// RUN: mlir-translate -no-implicit-module -test-spirv-roundtrip -split-input-file %s | FileCheck %s

// RUN: %if spirv-tools %{ rm -rf %t %}
// RUN: %if spirv-tools %{ mkdir %t %}
// RUN: %if spirv-tools %{ mlir-translate --no-implicit-module --serialize-spirv --split-input-file --spirv-save-validation-files-with-prefix=%t/module %s %}
// RUN: %if spirv-tools %{ spirv-val %t %}

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.func @copy_object_scalar(%arg0 : f32) -> f32 "None" {
    // CHECK: {{%.*}} = spirv.CopyObject {{%.*}} : f32
    %0 = spirv.CopyObject %arg0 : f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.func @copy_object_vector(%arg0 : vector<4xi32>) -> vector<4xi32> "None" {
    // CHECK: {{%.*}} = spirv.CopyObject {{%.*}} : vector<4xi32>
    %0 = spirv.CopyObject %arg0 : vector<4xi32>
    spirv.ReturnValue %0 : vector<4xi32>
  }
}

// -----

// `OpCopyLogical` was added in SPIR-V 1.4.
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [Shader, Linkage], []> {
  spirv.func @copy_logical(%arg0 : !spirv.struct<(i32, i32, !spirv.array<2 x i32>)>) -> !spirv.struct<(i32, i32, !spirv.array<2 x i32>)> "None" {
    // CHECK: {{%.*}} = spirv.CopyLogical {{%.*}} : !spirv.struct<(i32, i32, !spirv.array<2 x i32>)> to !spirv.struct<(i32, i32, !spirv.array<2 x i32>)>
    %0 = spirv.CopyLogical %arg0 : !spirv.struct<(i32, i32, !spirv.array<2 x i32>)> to !spirv.struct<(i32, i32, !spirv.array<2 x i32>)>
    spirv.ReturnValue %0 : !spirv.struct<(i32, i32, !spirv.array<2 x i32>)>
  }
}
