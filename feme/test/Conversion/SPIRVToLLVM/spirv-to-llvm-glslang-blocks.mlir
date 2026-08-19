// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks the shapes glslang itself emits for a storage/uniform buffer
// block, as opposed to FeMe's own upstream HLSL resource representation
// (see spirv-to-llvm-storage-buffer.mlir/spirv-to-llvm-uniform-buffer.mlir):
// glslang never wraps a block in an extra single-member struct, so its own
// `Block`/`BufferBlock`-decorated struct is free to declare more than one
// member directly.

// A pre-SPIR-V-1.3 SSBO: `Uniform` storage class, `BufferBlock` decoration,
// rather than `StorageBuffer`/`Block`. Otherwise identical to the ordinary
// single-member storage-buffer wrapper case.

// CHECK-LABEL: llvm.func @legacy_ssbo
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x f32>, 2, 1>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[PTR]] : !llvm.ptr<12> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @out bind(0, 1) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), BufferBlock>, Uniform>
  spirv.func @legacy_ssbo(%idx : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @out : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), BufferBlock>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), BufferBlock>, Uniform>, i32, i32 -> !spirv.ptr<f32, Uniform>
    %v = spirv.Load "Uniform" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

// -----

// A `Block`-decorated storage buffer struct with more than one member --
// a fixed header field alongside its trailing runtime array, the shape a
// plain GLSL `buffer B { uint count; float data[]; };` compiles to -- has
// no separate wrapper, so a header field access has only a single index,
// and the array's own member index (1) replaces the wrapper case's always-0
// leading one.

// CHECK-LABEL: llvm.func @header_field
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(i32, array<0 x f32>)>, 12, 1>
// CHECK: %[[FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<11> -> i32

// CHECK-LABEL: llvm.func @array_element
// CHECK: %[[HANDLE2:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(i32, array<0 x f32>)>, 12, 1>
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE2]], %{{.*}})
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, %{{.*}}]
// CHECK: llvm.load %[[ELEM]] : !llvm.ptr<11> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @b bind(0, 2) : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<f32, stride=4> [4]), Block>, StorageBuffer>
  spirv.func @header_field() -> i32 "None" {
    %0 = spirv.mlir.addressof @b : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<f32, stride=4> [4]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0] : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<f32, stride=4> [4]), Block>, StorageBuffer>, i32 -> !spirv.ptr<i32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : i32
    spirv.ReturnValue %v : i32
  }
  spirv.func @array_element(%idx : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @b : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<f32, stride=4> [4]), Block>, StorageBuffer>
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1, %idx] : !spirv.ptr<!spirv.struct<(i32 [0], !spirv.rtarray<f32, stride=4> [4]), Block>, StorageBuffer>, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

// -----

// A `Block`-decorated *uniform* struct with more than one member declared
// directly -- the shape a plain GLSL `uniform UBO { vec4 a; float b; };`
// compiles to, with no wrapper struct either.

// CHECK-LABEL: llvm.func @read_b
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(vector<4xf32>, f32)>, 2, 0>
// CHECK: %[[FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<12> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 3) : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], f32 [16]), Block>, Uniform>
  spirv.func @read_b() -> f32 "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], f32 [16]), Block>, Uniform>
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1] : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], f32 [16]), Block>, Uniform>, i32 -> !spirv.ptr<f32, Uniform>
    %v = spirv.Load "Uniform" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

// -----

// A sized-array member (rather than a storage buffer's dynamically-sized
// trailing one), the shape a plain GLSL `uniform UBO { float data[4]; };`
// compiles to: a single member, but a sized `spirv.array` rather than the
// homogeneous struct FeMe's own wrapper convention expects, so it is the
// direct (unwrapped) shape too.

// CHECK-LABEL: llvm.func @read_element
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(array<4 x f32>)>, 2, 0>
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, %{{.*}}]
// CHECK: llvm.load %[[ELEM]] : !llvm.ptr<12> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 4) : !spirv.ptr<!spirv.struct<(!spirv.array<4 x f32, stride=4> [0]), Block>, Uniform>
  spirv.func @read_element(%idx : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(!spirv.array<4 x f32, stride=4> [0]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.array<4 x f32, stride=4> [0]), Block>, Uniform>, i32, i32 -> !spirv.ptr<f32, Uniform>
    %v = spirv.Load "Uniform" %ac : f32
    spirv.ReturnValue %v : f32
  }
}
