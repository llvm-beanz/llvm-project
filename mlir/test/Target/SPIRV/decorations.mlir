// RUN: mlir-translate --no-implicit-module --split-input-file --test-spirv-roundtrip %s | FileCheck %s

// RUN: %if spirv-tools %{ rm -rf %t %}
// RUN: %if spirv-tools %{ mkdir %t %}
// RUN: %if spirv-tools %{ mlir-translate --no-implicit-module --serialize-spirv --split-input-file --spirv-save-validation-files-with-prefix=%t/module %s %}
// RUN: %if spirv-tools %{ spirv-val %t %}

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  // CHECK: location = 0 : i32
  spirv.GlobalVariable @var {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Input>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  // The deserializer used to reject this outright with "unhandled
  // Decoration : 'Component'", failing module deserialization before it
  // ever reached any interface variable packing a sub-Location component
  // offset.
  // CHECK: component = 2 : i32
  // CHECK: location = 0 : i32
  spirv.GlobalVariable @var {component = 2 : i32, location = 0 : i32} : !spirv.ptr<f32, Input>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  // CHECK: no_perspective
  spirv.GlobalVariable @var {no_perspective} : !spirv.ptr<vector<4xf32>, Input>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  // CHECK: flat
  spirv.GlobalVariable @var {flat} : !spirv.ptr<si32, Input>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  // The deserializer used to reject this outright with "unhandled
  // Decoration : 'Centroid'", failing module deserialization before it
  // ever reached any centroid-interpolated fragment input.
  // CHECK: centroid
  spirv.GlobalVariable @var {centroid} : !spirv.ptr<vector<4xf32>, Input>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], [SPV_KHR_variable_pointers]> {
  // CHECK: aliased
  // CHECK: aliased
  spirv.GlobalVariable @var1 bind(0, 0) {aliased} : !spirv.ptr<!spirv.struct<(!spirv.array<4xf32, stride=4>[0])>, StorageBuffer>
  spirv.GlobalVariable @var2 bind(0, 0) {aliased} : !spirv.ptr<!spirv.struct<(vector<4xf32>[0])>, StorageBuffer>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], [SPV_KHR_variable_pointers]> {
  // CHECK: non_readable
  spirv.GlobalVariable @var bind(0, 0) {non_readable} : !spirv.ptr<!spirv.struct<(!spirv.array<4xf32, stride=4>[0])>, StorageBuffer>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], [SPV_KHR_variable_pointers]> {
  // CHECK: non_writable
  spirv.GlobalVariable @var bind(0, 0) {non_writable} : !spirv.ptr<!spirv.struct<(!spirv.array<4xf32, stride=4>[0])>, StorageBuffer>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], [SPV_KHR_variable_pointers]> {
  // CHECK: restrict
  spirv.GlobalVariable @var bind(0, 0) {restrict} : !spirv.ptr<!spirv.struct<(!spirv.array<4xf32, stride=4>[0])>, StorageBuffer>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  // CHECK: relaxed_precision
  spirv.GlobalVariable @var {location = 0 : i32, relaxed_precision} : !spirv.ptr<vector<4xf32>, Output>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Tessellation, Linkage], []> {
  // CHECK: patch
  spirv.GlobalVariable @var {patch} : !spirv.ptr<vector<4xf32>, Input>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  // CHECK: invariant
  spirv.GlobalVariable @var {invariant} : !spirv.ptr<vector<2xf32>, Output>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  // CHECK: coherent
  spirv.GlobalVariable @var {coherent} : !spirv.ptr<vector<2xf32>, Output>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  // CHECK: volatile
  spirv.GlobalVariable @var {volatile} : !spirv.ptr<vector<2xf32>, Output>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  // CHECK: index = 42
  spirv.GlobalVariable @var {index = 42 : i32} : !spirv.ptr<vector<2xf32>, Output>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage, TransformFeedback], []> {
  // CHECK: offset = 0
  // CHECK-SAME: xfb_buffer = 0
  // CHECK-SAME: xfb_stride = 112
  spirv.GlobalVariable @spirv_var_64 {offset = 0 : i32, xfb_buffer = 0 : i32, xfb_stride = 112 : i32} : !spirv.ptr<vector<3xf32>, Output>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Geometry, GeometryStreams], []> {
  // CHECK: stream = 1
  spirv.GlobalVariable @var {stream = 1 : i32} : !spirv.ptr<vector<4xf32>, Output>
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  // CHECK: linkage_attributes = #spirv.linkage_attributes<linkage_name = "outSideGlobalVar1", linkage_type = <Import>>
  spirv.GlobalVariable @var1 {
    linkage_attributes=#spirv.linkage_attributes<
      linkage_name="outSideGlobalVar1", 
      linkage_type=<Import>
    >
  } : !spirv.ptr<f32, Private>
}

// -----

spirv.module Logical OpenCL requires #spirv.vce<v1.0, [Kernel, Linkage], [SPV_KHR_no_integer_wrap_decoration]> {
spirv.func @iadd_decorations(%arg: i32) -> i32 "None" {
  // CHECK: spirv.IAdd %{{.*}}, %{{.*}} {no_signed_wrap, no_unsigned_wrap}
  %0 = spirv.IAdd %arg, %arg {no_signed_wrap, no_unsigned_wrap} : i32
  spirv.ReturnValue %0 : i32
}
}

// -----

spirv.module Logical OpenCL requires #spirv.vce<v1.0, [Kernel, Linkage], []> {
spirv.func @fadd_decorations(%arg: f32) -> f32 "None" {
  // CHECK: spirv.FAdd %{{.*}}, %{{.*}} {fp_fast_math_mode = #spirv.fastmath_mode<NotNaN|NotInf|NSZ>}
  %0 = spirv.FAdd %arg, %arg {fp_fast_math_mode = #spirv.fastmath_mode<NotNaN|NotInf|NSZ>} : f32
  spirv.ReturnValue %0 : f32
}
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
spirv.func @fmul_decorations(%arg: f32) -> f32 "None" {
  // CHECK: spirv.FMul %{{.*}}, %{{.*}} {no_contraction}
  %0 = spirv.FMul %arg, %arg {no_contraction} : f32
  spirv.ReturnValue %0 : f32
}
}

// -----

spirv.module Logical OpenCL requires #spirv.vce<v1.0, [Kernel, Linkage, Float16], []> {
spirv.func @fp_rounding_mode(%arg: f32) -> f16 "None" {
  // CHECK: spirv.FConvert %arg0 {fp_rounding_mode = #spirv.fp_rounding_mode<RTN>} : f32 to f16
  %0 = spirv.FConvert %arg {fp_rounding_mode = #spirv.fp_rounding_mode<RTN>} : f32 to f16
  spirv.ReturnValue %0 : f16
}
}

// -----

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  // CHECK: spirv.func @relaxed_precision_arg({{%.*}}: !spirv.ptr<f32, Function> {spirv.decoration = #spirv.decoration<RelaxedPrecision>}) "None" attributes {relaxed_precision} {
  spirv.func @relaxed_precision_arg(%arg0: !spirv.ptr<f32, Function> {spirv.decoration = #spirv.decoration<RelaxedPrecision>}) -> () "None" attributes {relaxed_precision} {
    spirv.Return
  }
}

// -----

// A struct decorated with FPFastMathMode (rather than a struct member, or a
// value produced by an op) round-trips correctly: it used to crash the
// deserializer, since guessing the decoration back from its mangled
// attribute name conflated "FPFastMathMode" with the nonexistent
// "FpFastMathMode".
spirv.module Logical OpenCL requires #spirv.vce<v1.0, [Kernel, Linkage], []> {
  // CHECK: !spirv.ptr<!spirv.struct<(f32 [0], si32 [4]), FPFastMathMode=#spirv.fastmath_mode<NotNaN>>, Private>
  spirv.GlobalVariable @var : !spirv.ptr<!spirv.struct<(f32 [0], si32 [4]), FPFastMathMode=#spirv.fastmath_mode<NotNaN>>, Private>
}

// -----

// The `NonUniform` decoration (SPIR-V 1.5+, `ShaderNonUniform` capability)
// used to be entirely unhandled by both the deserializer ("unhandled
// Decoration : 'NonUniform'") and the serializer ("unhandled decoration
// NonUniform"), despite being a plain unit decoration exactly like
// `NoContraction`/`RelaxedPrecision`/... immediately above -- roadmap L7f
// (feme/docs/Roadmap.md) found this reduced from a real
// `NonUniformResourceIndex()`-using HLSL shader, whose dxc-compiled SPIR-V
// applies this decoration to an `OpCopyObject` result wrapping a dynamic
// resource-array index.
spirv.module Logical GLSL450 requires #spirv.vce<v1.5, [Shader, ShaderNonUniform], []> {
  spirv.func @non_uniform_decoration(%arg: i32) -> i32 "None" {
    // CHECK: spirv.IAdd %{{.*}}, %{{.*}} {non_uniform}
    %0 = spirv.IAdd %arg, %arg {non_uniform} : i32
    spirv.ReturnValue %0 : i32
  }
}

// -----

// The `Sample` decoration (a fragment-shader input's own per-sample, as
// opposed to per-pixel, interpolation qualifier -- GLSL's `sample in`)
// used to be entirely unhandled by both the deserializer ("unhandled
// Decoration : 'Sample'") and the serializer ("unhandled decoration
// Sample"), despite being a plain unit decoration exactly like
// `Centroid`/`NoPerspective`/`Flat` above -- feme roadmap L106's own CTS
// sweep of `dEQP-VK.pipeline.monolithic.multisample_shader_builtin.*`
// found this from a real fragment shader's own `sample`-qualified input.
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, SampleRateShading, Linkage], []> {
  // CHECK: sample
  spirv.GlobalVariable @var {sample} : !spirv.ptr<vector<4xf32>, Input>
}

