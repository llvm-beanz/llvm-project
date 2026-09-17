// RUN: mlir-translate -no-implicit-module -test-spirv-roundtrip -split-input-file %s | FileCheck %s

// RUN: %if spirv-tools %{ rm -rf %t %}
// RUN: %if spirv-tools %{ mkdir %t %}
// RUN: %if spirv-tools %{ mlir-translate --no-implicit-module --serialize-spirv --split-input-file --spirv-save-validation-files-with-prefix=%t/module %s %}
// RUN: %if spirv-tools %{ spirv-val %t %}

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DerivativeControl], []> {
  spirv.func @dpdx(%arg: f32) -> f32 "None" {
    // CHECK: spirv.DPdx {{%.*}} : f32
    %0 = spirv.DPdx %arg : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.func @dpdy(%arg: vector<3xf32>) -> vector<3xf32> "None" {
    // CHECK: spirv.DPdy {{%.*}} : vector<3xf32>
    %0 = spirv.DPdy %arg : vector<3xf32>
    spirv.ReturnValue %0 : vector<3xf32>
  }
  spirv.func @fwidth(%arg: f32) -> f32 "None" {
    // CHECK: spirv.Fwidth {{%.*}} : f32
    %0 = spirv.Fwidth %arg : f32
    spirv.ReturnValue %0 : f32
  }
  spirv.func @derivative_fine(%arg: f32) -> f32 "None" {
    // CHECK: spirv.DPdxFine {{%.*}} : f32
    %0 = spirv.DPdxFine %arg : f32
    // CHECK: spirv.DPdyFine {{%.*}} : f32
    %1 = spirv.DPdyFine %arg : f32
    // CHECK: spirv.FwidthFine {{%.*}} : f32
    %2 = spirv.FwidthFine %arg : f32
    spirv.ReturnValue %2 : f32
  }
  spirv.func @derivative_coarse(%arg: f32) -> f32 "None" {
    // CHECK: spirv.DPdxCoarse {{%.*}} : f32
    %0 = spirv.DPdxCoarse %arg : f32
    // CHECK: spirv.DPdyCoarse {{%.*}} : f32
    %1 = spirv.DPdyCoarse %arg : f32
    // CHECK: spirv.FwidthCoarse {{%.*}} : f32
    %2 = spirv.FwidthCoarse %arg : f32
    spirv.ReturnValue %2 : f32
  }
}
