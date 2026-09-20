// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H160: `spirv.ArrayLength` (`StructuredBuffer<T>::GetDimensions`/
// `ByteAddressBuffer::GetDimensions`, SPIR-V `OpArrayLength`) converts
// directly into a call to the generic `llvm.spv.resource.getarraylength`
// intrinsic against the already-converted handle value -- see
// `ArrayLengthPattern` (SPIRVToLLVMPatterns.cpp). `SPIRVResourceLowering.cpp`
// (the later, CPU-backend-specific pass) is what actually turns this
// intrinsic into a real element-count computation against the bound
// descriptor; see spirv-resource-lowering-array-length.ll for that half.

// CHECK-LABEL: llvm.func @array_length
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x i32>, 12, 1, 4>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getarraylength"(%[[HANDLE]])
// CHECK-SAME: -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 0) : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
  spirv.func @array_length() -> i32 "None" {
    %addr = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    %len = spirv.ArrayLength %addr[0] : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    spirv.ReturnValue %len : i32
  }
}

// -----

// Roadmap L124(a): a real, multi-field storage-buffer block (not a
// one-member wrapper) whose *last* member is the runtime array --
// `dEQP-VK.compute.pipeline.basic.read_unbound_ssbo`'s own real shape,
// `struct SSBO_1 { vec4 data; uint not_set[]; }`. `array_member` here is 1,
// not 0, but is still the struct's own final member index, so this
// converts the same way as the one-member-wrapper case above.

// CHECK-LABEL: llvm.func @array_length_real_block
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (vector<4xf32>, array<0 x i32>)>, 12, 1>
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getarraylength"(%[[HANDLE]])
// CHECK-SAME: -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 0) : !spirv.ptr<!spirv.struct<type.SSBO_1, (vector<4xf32> [0], !spirv.rtarray<i32, stride=4> [16]), Block>, StorageBuffer>
  spirv.func @array_length_real_block() -> i32 "None" {
    %addr = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<type.SSBO_1, (vector<4xf32> [0], !spirv.rtarray<i32, stride=4> [16]), Block>, StorageBuffer>
    %len = spirv.ArrayLength %addr[1] : !spirv.ptr<!spirv.struct<type.SSBO_1, (vector<4xf32> [0], !spirv.rtarray<i32, stride=4> [16]), Block>, StorageBuffer>
    spirv.ReturnValue %len : i32
  }
}
