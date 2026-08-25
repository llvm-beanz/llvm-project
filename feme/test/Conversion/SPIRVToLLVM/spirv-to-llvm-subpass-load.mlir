// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap F8a: a `subpassLoad()` (Dim::SubpassData `spirv.ImageRead`)
// converts directly into one `feme.stage.subpass.load` call per result
// component -- not the ordinary resource-handle load `ImageReadPattern`
// gives every other image dimension -- reading the currently-bound
// render-target attachment mapped to the variable's own
// `input_attachment_index` (here 2) rather than the descriptor-set image
// `llvm.spv.resource.handlefrombinding` still produces (left as dead code:
// note there is no `llvm.spv.resource.getpointer`/`llvm.load` pair below).

// CHECK-LABEL: llvm.func @load_subpass
// CHECK: llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[R:.*]] = llvm.call @feme.stage.subpass.load.f32(%{{.*}}, %{{.*}}) : (i32, i32) -> f32
// CHECK: llvm.insertelement %[[R]]
// CHECK: %[[G:.*]] = llvm.call @feme.stage.subpass.load.f32(%{{.*}}, %{{.*}}) : (i32, i32) -> f32
// CHECK: llvm.insertelement %[[G]]
// CHECK: %[[B:.*]] = llvm.call @feme.stage.subpass.load.f32(%{{.*}}, %{{.*}}) : (i32, i32) -> f32
// CHECK: llvm.insertelement %[[B]]
// CHECK: %[[A:.*]] = llvm.call @feme.stage.subpass.load.f32(%{{.*}}, %{{.*}}) : (i32, i32) -> f32
// CHECK: llvm.insertelement %[[A]]
// CHECK-NOT: llvm.call_intrinsic "llvm.spv.resource.getpointer"
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, InputAttachment], []> {
  spirv.GlobalVariable @in_color bind(0, 0) {input_attachment_index = 2 : i32} : !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
  spirv.func @load_subpass() -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @in_color : !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>
    %zero = spirv.Constant dense<0> : vector<2xi32>
    %2 = spirv.ImageRead %1, %zero : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, vector<2xi32> -> vector<4xf32>
    spirv.ReturnValue %2 : vector<4xf32>
  }
}

// -----

// A scalar (single-component) subpass read -- e.g. a depth input attachment
// -- builds no vector at all: the lone call's own result is the op's
// result.

// CHECK-LABEL: llvm.func @load_subpass_scalar
// CHECK: %[[R:.*]] = llvm.call @feme.stage.subpass.load.f32(%{{.*}}, %{{.*}}) : (i32, i32) -> f32
// CHECK: llvm.return %[[R]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, InputAttachment], []> {
  spirv.GlobalVariable @in_depth bind(0, 1) {input_attachment_index = 0 : i32} : !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
  spirv.func @load_subpass_scalar() -> f32 "None" {
    %0 = spirv.mlir.addressof @in_depth : !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>
    %zero = spirv.Constant dense<0> : vector<2xi32>
    %2 = spirv.ImageRead %1, %zero : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, vector<2xi32> -> f32
    spirv.ReturnValue %2 : f32
  }
}
