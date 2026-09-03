// RUN: mlir-translate --no-implicit-module --split-input-file --test-spirv-roundtrip %s | FileCheck %s

// RUN: %if spirv-tools %{ rm -rf %t %}
// RUN: %if spirv-tools %{ mkdir %t %}
// RUN: %if spirv-tools %{ mlir-translate --no-implicit-module --serialize-spirv --split-input-file --spirv-save-validation-files-with-prefix=%t/module %s %}
// RUN: %if spirv-tools %{ spirv-val %t %}

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, ImageQuery, Linkage], []> {
  spirv.func @image(%arg0 : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, %arg1 : vector<4xf32>, %arg2 : f32) "None" {
    // CHECK: {{%.*}} = spirv.Image {{%.*}} : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %0 = spirv.Image %arg0 : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    // CHECK: {{%.*}} = spirv.ImageDrefGather {{%.*}}, {{%.*}}, {{%.*}} : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>,  vector<4xf32>, f32 -> vector<4xf32>
    %1 = spirv.ImageDrefGather %arg0, %arg1, %arg2 : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>, vector<4xf32>, f32 -> vector<4xf32>
    spirv.Return
  }
  spirv.func @image_query_size(%arg0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>) "None" {
    // CHECK:  {{%.*}} = spirv.ImageQuerySize %arg0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown> -> vector<2xi32>
    %0 = spirv.ImageQuerySize %arg0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown> -> vector<2xi32>
    spirv.Return
  }
  spirv.func @image_read(%arg0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba8>, %arg1 : vector<2xsi32>) "None" {
    // CHECK: {{.*}} = spirv.ImageRead {{%.*}}, {{%.*}} : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba8>, vector<2xsi32> -> vector<4xf32>
    %0 = spirv.ImageRead %arg0, %arg1 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba8>, vector<2xsi32> -> vector<4xf32>
    spirv.Return
  }
  spirv.func @image_write(%arg0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba8>, %arg1 : vector<2xsi32>, %arg2 : vector<4xf32>) "None" {
    // CHECK: spirv.ImageWrite {{%.*}}, {{%.*}}, {{%.*}} : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba8>, vector<2xsi32>, vector<4xf32>
    spirv.ImageWrite %arg0, %arg1, %arg2 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba8>, vector<2xsi32>, vector<4xf32>
    spirv.Return
  }
  spirv.func @explicit_sample(%arg0 : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba8>>, %arg1 : vector<2xf32>, %arg2 : f32) "None" {
    // CHECK: {{%.*}} = spirv.ImageSampleExplicitLod {{%.*}}, {{%.*}} ["Lod"], {{%.*}} : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba8>>, vector<2xf32>, f32 -> vector<4xf32>
    %0 = spirv.ImageSampleExplicitLod %arg0, %arg1 ["Lod"], %arg2 : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba8>>, vector<2xf32>, f32 -> vector<4xf32>
    spirv.Return
  }
  spirv.func @implicit_sample(%arg0 : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba8>>, %arg1 : vector<3xf32>) "None" {
    // CHECK: {{%.*}} = spirv.ImageSampleImplicitLod {{%.*}}, {{%.*}} : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba8>>, vector<3xf32> -> vector<4xf32>
    %0 = spirv.ImageSampleImplicitLod %arg0, %arg1 : !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Rgba8>>, vector<3xf32> -> vector<4xf32>
    spirv.Return
  }
  spirv.func @implicit_sample_proj_dref(%arg0 : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Rgba8>>, %arg1 : vector<4xf32>, %arg2 : f32) "None" {
    // CHECK: {{%.*}} = spirv.ImageSampleProjDrefImplicitLod {{%.*}}, {{%.*}}, {{%.*}} : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Rgba8>>, vector<4xf32>, f32 -> f32
    %0 = spirv.ImageSampleProjDrefImplicitLod %arg0, %arg1, %arg2 : !spirv.sampled_image<!spirv.image<f32, Dim2D, IsDepth, NonArrayed, SingleSampled, NeedSampler, Rgba8>>, vector<4xf32>, f32 -> f32
    spirv.Return
  }
  spirv.func @image_fetch(%arg0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, %arg1 : vector<2xsi32>) "None" {
    // CHECK: {{%.*}} = spirv.ImageFetch {{%.*}}, {{%.*}} : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, vector<2xsi32> -> vector<4xf32>
    %0 = spirv.ImageFetch %arg0, %arg1 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, vector<2xsi32> -> vector<4xf32>
    spirv.Return
  }
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, StorageImageWriteWithoutFormat, StorageImageReadWithoutFormat, Linkage], []> {
  spirv.func @image_read_write(%arg0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, %arg1 : vector<2xsi32>) "None" {
    // CHECK: spirv.ImageRead {{%.*}}, {{%.*}} : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, vector<2xsi32> -> vector<4xf32>
    %0 = spirv.ImageRead %arg0, %arg1 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, vector<2xsi32> -> vector<4xf32>
    // CHECK: spirv.ImageWrite {{%.*}}, {{%.*}}, {{%.*}} : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, vector<2xsi32>, vector<4xf32>
    spirv.ImageWrite %arg0, %arg1, %0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, vector<2xsi32>, vector<4xf32>
    spirv.Return
  }
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.GlobalVariable @image bind(0, 0) : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>
  spirv.func @image_texel_pointer_atomic(%coord : vector<2xsi32>, %value : i32) -> i32 "None" {
    // CHECK: {{%.*}} = spirv.mlir.addressof @image
    %0 = spirv.mlir.addressof @image : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>
    %1 = spirv.Constant 0 : i32
    // CHECK: {{%.*}} = spirv.ImageTexelPointer {{%.*}}, {{%.*}}, {{%.*}} : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>, vector<2xsi32>, i32 -> !spirv.ptr<i32, Image>
    %2 = spirv.ImageTexelPointer %0, %coord, %1 : !spirv.ptr<!spirv.image<i32, Dim2D, NoDepth, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>, vector<2xsi32>, i32 -> !spirv.ptr<i32, Image>
    // CHECK: spirv.AtomicIAdd <Device> <None> {{%.*}}, {{%.*}} : !spirv.ptr<i32, Image>
    %3 = spirv.AtomicIAdd <Device> <None> %2, %value : !spirv.ptr<i32, Image>
    spirv.ReturnValue %3 : i32
  }
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.func @sampled_image(%arg0 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, %arg1 : !spirv.sampler) "None" {
    // CHECK: {{%.*}} = spirv.SampledImage {{%.*}}, {{%.*}} : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    %0 = spirv.SampledImage %arg0, %arg1 : !spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>, !spirv.sampler -> !spirv.sampled_image<!spirv.image<f32, Dim2D, NoDepth, NonArrayed, SingleSampled, NeedSampler, Unknown>>
    spirv.Return
  }
}
