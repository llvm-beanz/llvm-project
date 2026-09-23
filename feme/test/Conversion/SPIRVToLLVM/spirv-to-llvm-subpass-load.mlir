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
// CHECK: %[[R:.*]] = llvm.call @feme.stage.subpass.load.f32(%{{.*}}, %{{.*}}, %{{.*}}) : (i32, i32, i32) -> f32
// CHECK: llvm.insertelement %[[R]]
// CHECK: %[[G:.*]] = llvm.call @feme.stage.subpass.load.f32(%{{.*}}, %{{.*}}, %{{.*}}) : (i32, i32, i32) -> f32
// CHECK: llvm.insertelement %[[G]]
// CHECK: %[[B:.*]] = llvm.call @feme.stage.subpass.load.f32(%{{.*}}, %{{.*}}, %{{.*}}) : (i32, i32, i32) -> f32
// CHECK: llvm.insertelement %[[B]]
// CHECK: %[[A:.*]] = llvm.call @feme.stage.subpass.load.f32(%{{.*}}, %{{.*}}, %{{.*}}) : (i32, i32, i32) -> f32
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
// CHECK: %[[R:.*]] = llvm.call @feme.stage.subpass.load.f32(%{{.*}}, %{{.*}}, %{{.*}}) : (i32, i32, i32) -> f32
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

// -----

// Roadmap F8c: `subpassInputMS`'s explicit-sample `subpassLoad(input,
// sample)` form -- a `Dim::SubpassData` `spirv.ImageRead` with a lone
// `Sample` image operand -- threads that operand through as
// `feme.stage.subpass.load`'s third argument, rather than the constant `0`
// every implicit-sample read above synthesizes.

// CHECK-LABEL: llvm.func @load_subpass_multisample
// CHECK: %[[R:.*]] = llvm.call @feme.stage.subpass.load.f32(%{{.*}}, %{{.*}}, %{{.*}}) : (i32, i32, i32) -> f32
// CHECK: llvm.return %[[R]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, InputAttachment], []> {
  spirv.GlobalVariable @in_color_ms bind(0, 0) {input_attachment_index = 0 : i32} : !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, MultiSampled, NoSampler, Unknown>, UniformConstant>
  spirv.func @load_subpass_multisample(%sample : si32) -> f32 "None" {
    %0 = spirv.mlir.addressof @in_color_ms : !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, MultiSampled, NoSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, MultiSampled, NoSampler, Unknown>
    %zero = spirv.Constant dense<0> : vector<2xi32>
    %2 = spirv.ImageRead %1, %zero ["Sample"], %sample : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, MultiSampled, NoSampler, Unknown>, vector<2xi32>, si32 -> f32
    spirv.ReturnValue %2 : f32
  }
}

// -----

// Roadmap L178: an *array* of subpassInput variables (`layout
// (input_attachment_index = 1) uniform subpassInput uAttachments[3];`) --
// e.g. `descriptorset_random`'s own `ialimitlow` CTS coverage -- reads
// through one extra `spirv.AccessChain` selecting a compile-time-constant
// element between the `spirv.Load` and the `spirv.mlir.addressof`
// (`getSubpassVariable`'s `SubpassVariableAccess::ArrayIndexOffset`); per
// the Vulkan input-attachment model, array element N reads attachment
// index `InputAttachmentIndex + N` -- here `1 + 2 = 3`.

// CHECK-LABEL: llvm.func @load_subpass_array_elem
// CHECK: llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: llvm.mlir.constant(3 : i32)
// CHECK: %[[R:.*]] = llvm.call @feme.stage.subpass.load.f32(%{{.*}}, %{{.*}}, %{{.*}}) : (i32, i32, i32) -> f32
// CHECK: llvm.return %[[R]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, InputAttachment], []> {
  spirv.GlobalVariable @in_color_array bind(0, 0) {input_attachment_index = 1 : i32} : !spirv.ptr<!spirv.array<3 x !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>>, UniformConstant>
  spirv.func @load_subpass_array_elem() -> f32 "None" {
    %0 = spirv.mlir.addressof @in_color_array : !spirv.ptr<!spirv.array<3 x !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>>, UniformConstant>
    %idx = spirv.Constant 2 : i32
    %ac = spirv.AccessChain %0[%idx] : !spirv.ptr<!spirv.array<3 x !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>>, UniformConstant>, i32 -> !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %ac : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>
    %zero = spirv.Constant dense<0> : vector<2xi32>
    %2 = spirv.ImageRead %1, %zero : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, vector<2xi32> -> f32
    spirv.ReturnValue %2 : f32
  }
}

// -----

// Roadmap L179: an `isubpassInput` (signed-integer-format input
// attachment) reads back through `feme.stage.subpass.load.i32`, the
// `SubpassLoad` op's integer overload, rather than the default `.f32` one
// every prior case in this file uses.

// CHECK-LABEL: llvm.func @load_subpass_signed
// CHECK: %[[R:.*]] = llvm.call @feme.stage.subpass.load.i32(%{{.*}}, %{{.*}}, %{{.*}}) : (i32, i32, i32) -> i32
// CHECK: llvm.return %[[R]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, InputAttachment], []> {
  spirv.GlobalVariable @in_signed bind(0, 0) {input_attachment_index = 0 : i32} : !spirv.ptr<!spirv.image<si32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
  spirv.func @load_subpass_signed() -> si32 "None" {
    %0 = spirv.mlir.addressof @in_signed : !spirv.ptr<!spirv.image<si32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<si32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>
    %zero = spirv.Constant dense<0> : vector<2xi32>
    %2 = spirv.ImageRead %1, %zero : !spirv.image<si32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, vector<2xi32> -> si32
    spirv.ReturnValue %2 : si32
  }
}

// -----

// A non-constant array index into a subpassInput array is declined
// (`getSubpassVariable` returns `std::nullopt`) rather than miscompiled --
// `feme::StageOpKind::SubpassLoad`'s own `AttachmentIndex` operand is a
// host-side constant with no dynamic-index overload (see
// `getSubpassVariable`'s own comment) -- so `ImageReadPattern`'s ordinary,
// generic resource-image lowering handles this read instead.

// CHECK-LABEL: llvm.func @load_subpass_array_dynamic
// CHECK-NOT: llvm.call @feme.stage.subpass.load
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getpointer"
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, InputAttachment], []> {
  spirv.GlobalVariable @in_color_array_dyn bind(0, 0) {input_attachment_index = 1 : i32} : !spirv.ptr<!spirv.array<3 x !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>>, UniformConstant>
  spirv.func @load_subpass_array_dynamic(%idx : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @in_color_array_dyn : !spirv.ptr<!spirv.array<3 x !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>>, UniformConstant>
    %ac = spirv.AccessChain %0[%idx] : !spirv.ptr<!spirv.array<3 x !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>>, UniformConstant>, i32 -> !spirv.ptr<!spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %ac : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>
    %zero = spirv.Constant dense<0> : vector<2xi32>
    %2 = spirv.ImageRead %1, %zero : !spirv.image<f32, SubpassData, NoDepth, NonArrayed, SingleSampled, NoSampler, Unknown>, vector<2xi32> -> f32
    spirv.ReturnValue %2 : f32
  }
}
