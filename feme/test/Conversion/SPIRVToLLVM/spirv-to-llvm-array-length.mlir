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
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<0 x i32>, 12, 1>
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
