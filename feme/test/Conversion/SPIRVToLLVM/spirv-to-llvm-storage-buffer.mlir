// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a `StorageBuffer` block variable -- `RWStructuredBuffer<T>` in
// HLSL -- becomes the `llvm.spv.resource.handlefrombinding` call LLVM's
// SPIRV backend materializes a `spirv.VulkanBuffer` handle from, the same
// way an image resource does, and that indexing into it becomes
// `llvm.spv.resource.getpointer` rather than an ordinary GEP through a
// pointer nothing has bound to memory (see
// `llvm/test/CodeGen/SPIRV/pointers/structured-buffer-access.ll`).

// CHECK: llvm.mlir.global private constant @out.str("out\00")
// CHECK-NOT: llvm.mlir.global{{.*}}@out(
// CHECK-LABEL: llvm.func @rw
// CHECK: %[[SET:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[BINDING:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[NAME:.*]] = llvm.mlir.addressof @out.str : !llvm.ptr
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"(%[[SET]], %[[BINDING]], %{{.*}}, %{{.*}}, %[[NAME]])
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x vector<4xf32>>, 12, 1>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK-SAME: -> !llvm.ptr<11>
// CHECK: %[[VAL:.*]] = llvm.load %[[PTR]] : !llvm.ptr<11> -> vector<4xf32>
// CHECK: llvm.store %[[VAL]], %[[PTR]] : vector<4xf32>, !llvm.ptr<11>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @out bind(0, 1) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<vector<4xf32>, stride=16> [0])>, StorageBuffer>
  spirv.func @rw(%idx : i32) -> () "None" {
    %0 = spirv.mlir.addressof @out : !spirv.ptr<!spirv.struct<(!spirv.rtarray<vector<4xf32>, stride=16> [0])>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<vector<4xf32>, stride=16> [0])>, StorageBuffer>, i32, i32 -> !spirv.ptr<vector<4xf32>, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : vector<4xf32>
    spirv.Store "StorageBuffer" %ac, %v : vector<4xf32>
    spirv.Return
  }
}

// -----

// A `StructuredBuffer<T>` (SRV) decorates its sole member `NonWritable`,
// which becomes the handle type's `IsWriteable` parameter; indexing past
// the buffer element (into one of its fields) becomes an ordinary GEP off
// the pointer `llvm.spv.resource.getpointer` returns.

// CHECK-LABEL: llvm.func @read_field
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x struct<(vector<4xi32>, vector<4xf32>)>>, 12, 0>
// CHECK: %[[ELEM:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[FIELD:.*]] = llvm.getelementptr inbounds %[[ELEM]][0, 1]
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<11> -> vector<4xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @in bind(0, 1) : !spirv.ptr<!spirv.struct<(!spirv.rtarray<!spirv.struct<(vector<4xi32> [0], vector<4xf32> [16])>, stride=32> [0, NonWritable])>, StorageBuffer>
  spirv.func @read_field(%idx : i32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @in : !spirv.ptr<!spirv.struct<(!spirv.rtarray<!spirv.struct<(vector<4xi32> [0], vector<4xf32> [16])>, stride=32> [0, NonWritable])>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c0, %idx, %c1] : !spirv.ptr<!spirv.struct<(!spirv.rtarray<!spirv.struct<(vector<4xi32> [0], vector<4xf32> [16])>, stride=32> [0, NonWritable])>, StorageBuffer>, i32, i32, i32 -> !spirv.ptr<vector<4xf32>, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : vector<4xf32>
    spirv.ReturnValue %v : vector<4xf32>
  }
}

// -----

// A fixed-size (not runtime) array of 3-component vectors, as a
// `StorageBuffer` block member -- e.g. a vertex-attribute SSBO with a
// `float3 positions[4]` field -- must convert (and its `spirv.AccessChain`
// into an element legalize) even though the array's declared `ArrayStride`
// (16) doesn't match `vector<3xf32>`'s own compact size (12): 16 is that
// vector's Vulkan *base alignment*, the natural stride every conformant
// SPIR-V producer emits for such an array. See
// `getNaturalArrayStride` in `mlir/lib/Dialect/SPIRV/Utils/LayoutUtils.cpp`.

// CHECK-LABEL: llvm.func @read_vec3_array_element
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(array<4 x vector<3xf32>>)>, 12, 1>
// CHECK: %[[ELEM:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[FIELD:.*]] = llvm.getelementptr inbounds %[[ELEM]][0, %{{.*}}]
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<11> -> vector<3xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @positions bind(0, 1) : !spirv.ptr<!spirv.struct<(!spirv.array<4 x vector<3xf32>, stride=16> [0, NonWritable])>, StorageBuffer>
  spirv.func @read_vec3_array_element(%idx : i32) -> vector<3xf32> "None" {
    %0 = spirv.mlir.addressof @positions : !spirv.ptr<!spirv.struct<(!spirv.array<4 x vector<3xf32>, stride=16> [0, NonWritable])>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.array<4 x vector<3xf32>, stride=16> [0, NonWritable])>, StorageBuffer>, i32, i32 -> !spirv.ptr<vector<3xf32>, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : vector<3xf32>
    spirv.ReturnValue %v : vector<3xf32>
  }
}
