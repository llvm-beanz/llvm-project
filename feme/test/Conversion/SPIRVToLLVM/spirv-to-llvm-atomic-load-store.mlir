// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// `spirv.AtomicLoad`/`spirv.AtomicStore` against an ordinary converted
// memory pointer (here a `Uniform`-storage-class SSBO field, the shape
// `dEQP-VK.memory_model.message_passing.permuted_index.*` exercises) --
// unlike every other `spirv.Atomic*` op this converter supports, these two
// are not restricted to an `ImageTexelPointerPattern`-produced storage-
// image texel pointer; see AtomicLoadPattern/AtomicStorePattern's own
// comment (SPIRVToLLVMPatterns.cpp). `llvm.load`/`llvm.store` require an
// explicit alignment to be atomic at all, computed here as the loaded/
// stored scalar's own natural (byte-rounded) size.

// CHECK-LABEL: llvm.func @atomic_load_store
// CHECK: %[[FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: %[[LOADED:.*]] = llvm.load %[[FIELD]] atomic acquire <alignment = 4> : !llvm.ptr<12> -> i32
// CHECK: llvm.store %[[LOADED]], %[[FIELD]] atomic release <alignment = 4> : i32, !llvm.ptr<12>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 2) : !spirv.ptr<!spirv.struct<(!spirv.struct<(f32 [0], i32 [4])> [0]), Block>, Uniform>
  spirv.func @atomic_load_store() -> i32 "None" {
    %0 = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(!spirv.struct<(f32 [0], i32 [4])> [0]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c0, %c1] : !spirv.ptr<!spirv.struct<(!spirv.struct<(f32 [0], i32 [4])> [0]), Block>, Uniform>, i32, i32 -> !spirv.ptr<i32, Uniform>
    %v = spirv.AtomicLoad <Device> <Acquire|UniformMemory> %ac : !spirv.ptr<i32, Uniform>
    spirv.AtomicStore <Device> <Release|UniformMemory> %ac, %v : !spirv.ptr<i32, Uniform>
    spirv.ReturnValue %v : i32
  }
}
