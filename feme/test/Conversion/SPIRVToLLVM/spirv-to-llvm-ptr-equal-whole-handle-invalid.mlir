// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file %s

// Roadmap K6p: `spirv.PtrEqual`/`spirv.PtrNotEqual` legally compare two
// *whole* resource handles directly (no intervening `spirv.AccessChain`),
// e.g. the `dEQP-VK.spirv_assembly.instruction.spirv1p4.opptrequal.
// different_ssbos_equal` CTS case's two `StorageBuffer`-class
// `OpVariable`s. Upstream MLIR's generic `IComparePattern` (reused for
// `OpPtrEqual`/`OpPtrNotEqual`, `SPIRVToLLVM.cpp`) only knows how to lower
// to `llvm.icmp`, which the LLVM dialect verifier only accepts on a
// (possibly vector-of-)signless-integer or LLVM-pointer operand; this
// ICD's own custom type converter maps a `StorageBuffer`-class resource
// variable's whole-handle type to an opaque
// `!llvm.target<"spirv.VulkanBuffer", ...>`, not a real pointer. Before
// `IComparePattern`'s own operand-type guard, this emitted an invalid
// `llvm.icmp` the LLVM dialect verifier rejected outright (a hard crash
// downstream, not a clean legalization failure); the pattern now declines
// to match this shape instead, leaving the op to fail legalization
// cleanly like any other genuinely unsupported shape -- comparing such
// handles is a FeMe CPU-backend-level resource-index concern (see
// `SPIRVResourceLowering.cpp`'s own `mergeIntoCompare`), not something
// this generic upstream pattern can lower.

spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [Shader, VariablePointers], [SPV_KHR_variable_pointers]> {
  spirv.GlobalVariable @var1 bind(0, 0) : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
  spirv.GlobalVariable @var2 bind(0, 1) : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
  spirv.func @whole_handle_ptr_equal() -> i1 "None" {
    %0 = spirv.mlir.addressof @var1 : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    %1 = spirv.mlir.addressof @var2 : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    // expected-error@+1 {{failed to legalize operation 'spirv.PtrEqual' that was explicitly marked illegal}}
    %2 = spirv.PtrEqual %0, %1 : !spirv.ptr<!spirv.struct<type.StructuredBuffer, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    spirv.ReturnValue %2 : i1
  }
}
