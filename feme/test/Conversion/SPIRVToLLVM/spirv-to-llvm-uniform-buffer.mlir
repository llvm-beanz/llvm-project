// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a `Uniform` storage-class block variable -- `cbuffer`/
// `ConstantBuffer<T>` in HLSL -- becomes the same
// `llvm.spv.resource.handlefrombinding` call a storage buffer's does, but
// over a handle wrapping its own field struct directly rather than a
// runtime array, and that a field access becomes
// `llvm.spv.resource.getpointer` with the field's own struct index, rather
// than an ordinary GEP through a pointer nothing has bound to memory.

// CHECK: llvm.mlir.global private constant @cb.str("cb\00")
// CHECK-NOT: llvm.mlir.global{{.*}}@cb(
// CHECK-LABEL: llvm.func @read_field
// CHECK: %[[SET:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[BINDING:.*]] = llvm.mlir.constant(2 : i32) : i32
// CHECK: %[[NAME:.*]] = llvm.mlir.addressof @cb.str : !llvm.ptr
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"(%[[SET]], %[[BINDING]], %{{.*}}, %{{.*}}, %[[NAME]])
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(f32, i32)>, 2, 0>
// CHECK: %[[FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK-SAME: -> !llvm.ptr<12>
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<12> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 2) : !spirv.ptr<!spirv.struct<(!spirv.struct<(f32 [0], i32 [4])> [0])>, Uniform>
  spirv.func @read_field() -> i32 "None" {
    %0 = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(!spirv.struct<(f32 [0], i32 [4])> [0])>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c0, %c1] : !spirv.ptr<!spirv.struct<(!spirv.struct<(f32 [0], i32 [4])> [0])>, Uniform>, i32, i32 -> !spirv.ptr<i32, Uniform>
    %v = spirv.Load "Uniform" %ac : i32
    spirv.ReturnValue %v : i32
  }
}

// -----

// A `Block`-decorated uniform block: real (`dxc`-compiled, or any
// binary-round-tripped) SPIR-V always decorates both the wrapper and its
// sole member's own struct with explicit `Offset` decorations -- unlike the
// hand-written module above, which omits both since MLIR's text parser does
// not require them (see `convertOffsetStructTypeIgnoringDecorations`'s
// comment in SPIRVToLLVMPatterns.cpp). Identical output to the case above,
// decoration aside.

// CHECK-LABEL: llvm.func @read_field
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(f32, i32)>, 2, 0>
// CHECK: %[[FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<12> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 2) : !spirv.ptr<!spirv.struct<(!spirv.struct<(f32 [0], i32 [4]), Block> [0]), Block>, Uniform>
  spirv.func @read_field() -> i32 "None" {
    %0 = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(!spirv.struct<(f32 [0], i32 [4]), Block> [0]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c0, %c1] : !spirv.ptr<!spirv.struct<(!spirv.struct<(f32 [0], i32 [4]), Block> [0]), Block>, Uniform>, i32, i32 -> !spirv.ptr<i32, Uniform>
    %v = spirv.Load "Uniform" %ac : i32
    spirv.ReturnValue %v : i32
  }
}
