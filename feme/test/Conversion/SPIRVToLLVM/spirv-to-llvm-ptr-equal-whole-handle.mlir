// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap K6t: `spirv.PtrEqual`/`spirv.PtrNotEqual` legally compare two
// *whole* resource handles directly (no intervening `spirv.AccessChain`),
// e.g. the `dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrequal.
// different_ssbos_equal` CTS case's two `StorageBuffer`-class
// `OpVariable`s. Upstream MLIR's generic `IComparePattern` (reused for
// `OpPtrEqual`/`OpPtrNotEqual`, `SPIRVToLLVM.cpp`) only knows how to lower
// to `llvm.icmp`, which the LLVM dialect verifier only accepts on a
// (possibly vector-of-)signless-integer or LLVM-pointer operand; this
// ICD's own custom type converter maps a `StorageBuffer`-class resource
// variable's whole-handle type to an opaque
// `!llvm.target<"spirv.VulkanBuffer", ...>`, not a real pointer, so
// `IComparePattern` correctly declines this shape rather than emitting an
// invalid `llvm.icmp`. `HandleCompareConversionPattern` (FeMe-local,
// `SPIRVToLLVMPatterns.cpp`) handles it instead, lowering to a call of the
// new `llvm.spv.resource.handleequal` intrinsic that
// `SPIRVResourceLowering.cpp`'s own `mergeIntoHandleCompare` resolves into
// a plain descriptor-index comparison (roadmap K6t).

// CHECK-LABEL: llvm.func @whole_handle_ptr_equal
// CHECK: %[[HA:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[HB:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[EQ:.*]] = llvm.call_intrinsic "llvm.spv.resource.handleequal"(%[[HA]], %[[HB]])
// CHECK: llvm.return %[[EQ]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [Shader, VariablePointers], [SPV_KHR_variable_pointers]> {
  spirv.GlobalVariable @var1 bind(0, 0) : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
  spirv.GlobalVariable @var2 bind(0, 1) : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
  spirv.func @whole_handle_ptr_equal() -> i1 "None" {
    %0 = spirv.mlir.addressof @var1 : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    %1 = spirv.mlir.addressof @var2 : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    %2 = spirv.PtrEqual %0, %1 : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    spirv.ReturnValue %2 : i1
  }
}

// -----

// `spirv.PtrNotEqual` negates the same intrinsic's result with a plain
// `xor` (rather than needing a second intrinsic) -- see
// `HandleCompareConversionPattern`'s own `Negate` comment.

// CHECK-LABEL: llvm.func @whole_handle_ptr_not_equal
// CHECK: %[[HA:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[HB:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[EQ:.*]] = llvm.call_intrinsic "llvm.spv.resource.handleequal"(%[[HA]], %[[HB]])
// CHECK: %[[TRUE:.*]] = llvm.mlir.constant(true)
// CHECK: %[[NE:.*]] = llvm.xor %[[EQ]], %[[TRUE]]
// CHECK: llvm.return %[[NE]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [Shader, VariablePointers], [SPV_KHR_variable_pointers]> {
  spirv.GlobalVariable @var1 bind(0, 0) : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
  spirv.GlobalVariable @var2 bind(0, 1) : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
  spirv.func @whole_handle_ptr_not_equal() -> i1 "None" {
    %0 = spirv.mlir.addressof @var1 : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    %1 = spirv.mlir.addressof @var2 : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    %2 = spirv.PtrNotEqual %0, %1 : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    spirv.ReturnValue %2 : i1
  }
}

// -----

// A whole-handle comparison against `OpConstantNull` (`null_comparisons_
// ssbo_equal`'s own shape) still matches `HandleCompareConversionPattern`:
// the match condition is the op's own operand type (a resource-handle
// target-extension type), which a converted `OpConstantNull` of that same
// pointer type also has -- see `ConstantNullPointerPattern`, upstream
// `SPIRVToLLVM.cpp`, which converts it to `llvm.mlir.zero` first.

// CHECK-LABEL: llvm.func @ptr_equal_vs_null
// CHECK: %[[HA:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[NULL:.*]] = llvm.mlir.zero
// CHECK: %[[EQ:.*]] = llvm.call_intrinsic "llvm.spv.resource.handleequal"(%[[HA]], %[[NULL]])
// CHECK: llvm.return %[[EQ]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [Shader, VariablePointers], [SPV_KHR_variable_pointers]> {
  spirv.GlobalVariable @var1 bind(0, 0) : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
  spirv.func @ptr_equal_vs_null() -> i1 "None" {
    %0 = spirv.mlir.addressof @var1 : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    %null = spirv.Constant unit : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    %1 = spirv.PtrEqual %0, %null : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    spirv.ReturnValue %1 : i1
  }
}
