// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a SPIR-V resource variable becomes the
// `llvm.spv.resource.handlefrombinding` call LLVM's SPIRV backend emits the
// `OpVariable` and its `DescriptorSet`/`Binding` decorations from, rather than
// an LLVM global nothing ever defines. This is the same representation the
// DXIL -> SPIR-V direction produces (feme::spirv::RaisedLoweringPass), so
// both front ends agree on how a resource handle is spelled.

// CHECK: llvm.mlir.global private constant @buf.str("buf\00")
// CHECK-NOT: llvm.mlir.global{{.*}}@buf(
// CHECK-LABEL: llvm.func @load_handle
// CHECK: %[[SET:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[BINDING:.*]] = llvm.mlir.constant(3 : i32) : i32
// CHECK: %[[SIZE:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[INDEX:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[NAME:.*]] = llvm.mlir.addressof @buf.str : !llvm.ptr
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"(%[[SET]], %[[BINDING]], %[[SIZE]], %[[INDEX]], %[[NAME]])
// CHECK-SAME: -> !llvm.target<"spirv.Image", f32, 5, 0, 0, 0, 2, 1>
// CHECK: llvm.return %[[HANDLE]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @buf bind(0, 3) : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
  spirv.func @load_handle() -> !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f> "None" {
    %0 = spirv.mlir.addressof @buf : !spirv.ptr<!spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>
    spirv.ReturnValue %1 : !spirv.image<f32, Buffer, NoDepth, NonArrayed, SingleSampled, NoSampler, Rgba32f>
  }
}

// -----

// A read-only (`Sampled` = 1) image of signed integers uses the dedicated
// `spirv.SignedImage` handle type; its binding is carried the same way.

// CHECK: llvm.mlir.global private constant @srv.str("srv\00")
// CHECK-LABEL: llvm.func @load_signed_handle
// CHECK: llvm.mlir.constant(2 : i32) : i32
// CHECK: llvm.mlir.constant(7 : i32) : i32
// CHECK: llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.SignedImage", i32, 5, 0, 0, 0, 1, 24>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @srv bind(2, 7) : !spirv.ptr<!spirv.image<si32, Buffer, NoDepth, NonArrayed, SingleSampled, NeedSampler, R32i>, UniformConstant>
  spirv.func @load_signed_handle() -> !spirv.image<si32, Buffer, NoDepth, NonArrayed, SingleSampled, NeedSampler, R32i> "None" {
    %0 = spirv.mlir.addressof @srv : !spirv.ptr<!spirv.image<si32, Buffer, NoDepth, NonArrayed, SingleSampled, NeedSampler, R32i>, UniformConstant>
    %1 = spirv.Load "UniformConstant" %0 : !spirv.image<si32, Buffer, NoDepth, NonArrayed, SingleSampled, NeedSampler, R32i>
    spirv.ReturnValue %1 : !spirv.image<si32, Buffer, NoDepth, NonArrayed, SingleSampled, NeedSampler, R32i>
  }
}
