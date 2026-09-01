// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks arrayed resource (image/sampled-image/sampler) bindings --
// `RWBuffer<T> Buf[N]` in HLSL -- Roadmap.md's L12a. Unlike an arrayed
// *block* binding (see spirv-to-llvm-arrayed-blocks.mlir), whose element is
// a memory-backed struct needing further byte-offset indexing once the
// right descriptor is selected, an arrayed *resource*'s element is itself
// the whole opaque handle value, so its own access chain converts directly
// into `llvm.spv.resource.handlefrombinding`, with the chain's own leading
// index becoming that intrinsic's `Index` operand (see
// feme::spirv::ResourceArrayAccessChainPattern).

// A compile-time-bounded array of image resources.

// CHECK-LABEL: llvm.func @read_elem
// CHECK: %[[SET:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[BINDING:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[COUNT:.*]] = llvm.mlir.constant(3 : i32) : i32
// CHECK: %[[NAME:.*]] = llvm.mlir.addressof @Buf.str : !llvm.ptr
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"(%[[SET]], %[[BINDING]], %[[COUNT]], %arg0, %[[NAME]])
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %arg1)
// CHECK: llvm.load %[[PTR]] : !llvm.ptr -> vector<4xi32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, SampledBuffer, ImageBuffer], []> {
  spirv.GlobalVariable @Buf bind(0, 1) : !spirv.ptr<!spirv.array<3 x !spirv.image<si32, Buffer, DepthUnknown, NonArrayed, SingleSampled, NoSampler, R32i>>, UniformConstant>
  spirv.func @read_elem(%idx : i32, %coord : i32) -> vector<4xsi32> "None" {
    %0 = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.array<3 x !spirv.image<si32, Buffer, DepthUnknown, NonArrayed, SingleSampled, NoSampler, R32i>>, UniformConstant>
    %ac = spirv.AccessChain %0[%idx] : !spirv.ptr<!spirv.array<3 x !spirv.image<si32, Buffer, DepthUnknown, NonArrayed, SingleSampled, NoSampler, R32i>>, UniformConstant>, i32 -> !spirv.ptr<!spirv.image<si32, Buffer, DepthUnknown, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>
    %v = spirv.Load "UniformConstant" %ac : !spirv.image<si32, Buffer, DepthUnknown, NonArrayed, SingleSampled, NoSampler, R32i>
    %r = spirv.ImageRead %v, %coord ["None"] : !spirv.image<si32, Buffer, DepthUnknown, NonArrayed, SingleSampled, NoSampler, R32i>, i32 -> vector<4xsi32>
    spirv.ReturnValue %r : vector<4xsi32>
  }
}

// -----

// An unbounded (runtime-sized) array of image resources -- `RWBuffer<T>
// Buf[]` in HLSL. Converts identically to the bounded case above, except
// `Count` is `0`, this map's own reserved sentinel meaning "unbounded,"
// rather than a real descriptor count (see ResourceInfo::Count).

// CHECK-LABEL: llvm.func @read_elem_unbounded
// CHECK: %[[SET:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[BINDING:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[COUNT:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[NAME:.*]] = llvm.mlir.addressof @Buf.str : !llvm.ptr
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"(%[[SET]], %[[BINDING]], %[[COUNT]], %arg0, %[[NAME]])
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %arg1)
// CHECK: llvm.load %[[PTR]] : !llvm.ptr -> vector<4xi32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, SampledBuffer, ImageBuffer, RuntimeDescriptorArray], [SPV_EXT_descriptor_indexing]> {
  spirv.GlobalVariable @Buf bind(0, 1) : !spirv.ptr<!spirv.rtarray<!spirv.image<si32, Buffer, DepthUnknown, NonArrayed, SingleSampled, NoSampler, R32i>>, UniformConstant>
  spirv.func @read_elem_unbounded(%idx : i32, %coord : i32) -> vector<4xsi32> "None" {
    %0 = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.rtarray<!spirv.image<si32, Buffer, DepthUnknown, NonArrayed, SingleSampled, NoSampler, R32i>>, UniformConstant>
    %ac = spirv.AccessChain %0[%idx] : !spirv.ptr<!spirv.rtarray<!spirv.image<si32, Buffer, DepthUnknown, NonArrayed, SingleSampled, NoSampler, R32i>>, UniformConstant>, i32 -> !spirv.ptr<!spirv.image<si32, Buffer, DepthUnknown, NonArrayed, SingleSampled, NoSampler, R32i>, UniformConstant>
    %v = spirv.Load "UniformConstant" %ac : !spirv.image<si32, Buffer, DepthUnknown, NonArrayed, SingleSampled, NoSampler, R32i>
    %r = spirv.ImageRead %v, %coord ["None"] : !spirv.image<si32, Buffer, DepthUnknown, NonArrayed, SingleSampled, NoSampler, R32i>, i32 -> vector<4xsi32>
    spirv.ReturnValue %r : vector<4xsi32>
  }
}
