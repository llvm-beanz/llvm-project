// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L124(b): a wrapper-shape storage buffer block (see
// BlockAccessChainPattern's own comment) whose `spirv.AccessChain` has
// *only* the wrapper-selecting index (always the constant 0) and no
// further, real per-element index -- a legal, if degenerate, whole-array
// access producing a pointer to the entire runtime array, not any one
// element of it. `dEQP-VK.compute.pipeline.basic.remove_global_load_pass`'s
// own Tint-generated shader emits exactly this shape as a dead value (never
// loaded from), which previously failed to legalize with "not enough
// indices" since BlockAccessChainPattern unconditionally required a second,
// real index following the wrapper selector.

// CHECK-LABEL: llvm.func @whole_array
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %[[ZERO]])
// CHECK-SAME: -> !llvm.ptr<11>
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader], [SPV_KHR_storage_buffer_storage_class]> {
  spirv.GlobalVariable @outputs bind(0, 0) : !spirv.ptr<!spirv.struct<Outputs, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
  spirv.func @whole_array() "None" {
    %outputs = spirv.mlir.addressof @outputs : !spirv.ptr<!spirv.struct<Outputs, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %ptr = spirv.AccessChain %outputs[%c0] : !spirv.ptr<!spirv.struct<Outputs, (!spirv.rtarray<i32, stride=4> [0]), Block>, StorageBuffer>, i32 -> !spirv.ptr<!spirv.rtarray<i32, stride=4>, StorageBuffer>
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @whole_array
  spirv.ExecutionMode @whole_array "LocalSize", 1, 1, 1
}
