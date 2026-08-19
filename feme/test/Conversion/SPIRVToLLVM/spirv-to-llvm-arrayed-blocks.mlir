// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks arrayed bindings -- `T blocks[N]` in GLSL, a single binding
// covering `N` descriptors, each its own storage/uniform buffer block
// instance -- the second half of Roadmap.md's C2. Unlike a non-arrayed
// block, whose handle needs no runtime information the type converter
// cannot already supply, an arrayed block's handle needs *which*
// descriptor to bind, only known at its own access chain's leading (array)
// index, so `spirv.mlir.addressof`'s own conversion just erases the
// variable's address instead of building a handle for it (see
// feme::spirv::ResourceAddressOfPattern).

// An array of storage buffer blocks.

// CHECK-LABEL: llvm.func @read_elem
// CHECK: %[[SET:.*]] = llvm.mlir.constant(2 : i32) : i32
// CHECK: %[[BINDING:.*]] = llvm.mlir.constant(6 : i32) : i32
// CHECK: %[[COUNT:.*]] = llvm.mlir.constant(4 : i32) : i32
// CHECK: %[[NAME:.*]] = llvm.mlir.addressof @buffers.str : !llvm.ptr
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"(%[[SET]], %[[BINDING]], %[[COUNT]], %arg0, %[[NAME]])
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x f32>, 12, 1>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %arg1)
// CHECK: llvm.load %[[PTR]] : !llvm.ptr<11> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @buffers bind(2, 6) : !spirv.ptr<!spirv.array<4 x !spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), Block>>, StorageBuffer>
  spirv.func @read_elem(%bufIdx : i32, %elemIdx : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @buffers : !spirv.ptr<!spirv.array<4 x !spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), Block>>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%bufIdx, %c0, %elemIdx] : !spirv.ptr<!spirv.array<4 x !spirv.struct<(!spirv.rtarray<f32, stride=4> [0]), Block>>, StorageBuffer>, i32, i32, i32 -> !spirv.ptr<f32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

// -----

// An array of uniform blocks, whose own direct (unwrapped) shape has no
// dummy leading index to drop -- the array index alone shifts the field
// selector over by one, unlike the wrapper shape's own array index *and*
// dummy selector above.

// CHECK-LABEL: llvm.func @read_field
// CHECK: %[[SET:.*]] = llvm.mlir.constant(5 : i32) : i32
// CHECK: %[[BINDING:.*]] = llvm.mlir.constant(2 : i32) : i32
// CHECK: %[[COUNT:.*]] = llvm.mlir.constant(3 : i32) : i32
// CHECK: %[[NAME:.*]] = llvm.mlir.addressof @ubos.str : !llvm.ptr
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"(%[[SET]], %[[BINDING]], %[[COUNT]], %arg0, %[[NAME]])
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(vector<4xf32>, f32)>, 2, 0>
// CHECK: %[[FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<12> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubos bind(5, 2) : !spirv.ptr<!spirv.array<3 x !spirv.struct<(vector<4xf32> [0], f32 [16]), Block>>, Uniform>
  spirv.func @read_field(%idx : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @ubos : !spirv.ptr<!spirv.array<3 x !spirv.struct<(vector<4xf32> [0], f32 [16]), Block>>, Uniform>
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%idx, %c1] : !spirv.ptr<!spirv.array<3 x !spirv.struct<(vector<4xf32> [0], f32 [16]), Block>>, Uniform>, i32, i32 -> !spirv.ptr<f32, Uniform>
    %v = spirv.Load "Uniform" %ac : f32
    spirv.ReturnValue %v : f32
  }
}
