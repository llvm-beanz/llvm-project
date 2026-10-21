// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L452(d): a direct `spirv.Load`/`spirv.Store`/`spirv.CopyMemory`
// on a storage/uniform buffer block's own base pointer, with no
// intervening `spirv.AccessChain` at all -- the shape
// `dEQP-VK.spirv_assembly.instruction.compute.opcopymemory.struct` hits
// (glslang's own direct multi-member-struct block convention, no wrapper).
// `BlockAccessChainPattern`/`ArrayedBlockAccessChainPattern` only convert
// an `AccessChain`, which always carries at least one index value to pass
// to `llvm.spv.resource.getpointer`; a bare whole-block op has none, so it
// used to fall through to MLIR's own generic `LoadOp`/`StoreOp`/
// `CopyMemoryOp` patterns, which assume an ordinary `!llvm.ptr` operand and
// fail legalization outright on the untranslated `spirv.VulkanBuffer`
// handle type instead.
//
// Each block's own offset-0 member (`vector<4xf32>`) is the one
// `BlockWholeLoadPattern`/`BlockWholeStorePattern`/
// `BlockWholeCopyMemoryPattern` address directly via
// `llvm.spv.resource.getpointer(Handle, 0)`, since (in LLVM's
// opaque-pointer model) that member's own address coincides exactly with
// the whole block's base address.

// CHECK-LABEL: llvm.func @load_whole_block
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (vector<4xf32>, vector<4xf32>)>, 2, 0>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[PTR]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @indata bind(0, 0) : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block>, Uniform>
  spirv.func @load_whole_block() -> !spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block> "None" {
    %in = spirv.mlir.addressof @indata : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block>, Uniform>
    %v = spirv.Load "Uniform" %in : !spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block>
    spirv.ReturnValue %v : !spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block>
  }
}

// -----

// CHECK-LABEL: llvm.func @store_whole_block
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (vector<4xf32>, vector<4xf32>)>, 12, 1>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.store %{{.*}}, %[[PTR]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @outdata bind(0, 1) : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block>, StorageBuffer>
  spirv.func @store_whole_block(%v : !spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block>) -> () "None" {
    %out = spirv.mlir.addressof @outdata : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block>, StorageBuffer>
    spirv.Store "StorageBuffer" %out, %v : !spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block>
    spirv.Return
  }
}

// -----

// CHECK-LABEL: llvm.func @copy_whole_block
// CHECK: %[[IN:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (vector<4xf32>, vector<4xf32>)>, 2, 0>
// CHECK: %[[OUT:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (vector<4xf32>, vector<4xf32>)>, 12, 1>
// CHECK: %[[SRC:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[IN]], %{{.*}})
// CHECK: %[[DST:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[OUT]], %{{.*}})
// CHECK: %[[LOADED:.*]] = llvm.load %[[SRC]]
// CHECK: llvm.store %[[LOADED]], %[[DST]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @indata bind(0, 0) : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block>, Uniform>
  spirv.GlobalVariable @outdata bind(0, 1) : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block>, StorageBuffer>
  spirv.func @copy_whole_block() -> () "None" {
    %in = spirv.mlir.addressof @indata : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block>, Uniform>
    %out = spirv.mlir.addressof @outdata : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block>, StorageBuffer>
    spirv.CopyMemory "StorageBuffer" %out, "Uniform" %in : !spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), Block>
    spirv.Return
  }
}

// -----

// Roadmap L452(e): `dEQP-VK.spirv_assembly.instruction.compute.
// opcopymemory.struct`'s own real shape -- unlike `copy_whole_block`
// above, whose `spirv.CopyMemory` copies directly between two resource
// handles, this one routes the whole-block content through an
// intermediate `Function`-storage local variable, one `CopyMemory` per
// leg: handle -> local, then local -> handle. Each leg individually has
// exactly one handle-typed side and one already-a-real-pointer side --
// structurally indistinguishable, before this fix, from
// `StageIOCopyMemoryPattern`'s own scalar `Input`-variable eager-load
// passthrough shape (`spirv-to-llvm-terminate-invocation.mlir`), which
// claimed this op first and stored the raw resource handle itself into
// the local variable rather than resolving its content pointer via
// `BlockWholeCopyMemoryPattern`.

// CHECK-LABEL: llvm.func @copy_via_local
// CHECK: %[[LOCAL:.*]] = llvm.alloca
// CHECK: %[[IN:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (vector<4xf32>, vector<4xf32>)>, 12, 1>
// CHECK: %[[OUT:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (vector<4xf32>, vector<4xf32>)>, 12, 1>
// CHECK: %[[SRC:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[IN]], %{{.*}})
// CHECK: %[[LOADED1:.*]] = llvm.load %[[SRC]]
// CHECK: llvm.store %[[LOADED1]], %[[LOCAL]]
// CHECK: %[[DST:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[OUT]], %{{.*}})
// CHECK: %[[LOADED2:.*]] = llvm.load %[[LOCAL]]
// CHECK: llvm.store %[[LOADED2]], %[[DST]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @indata bind(0, 0) : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), BufferBlock>, Uniform>
  spirv.GlobalVariable @outdata bind(0, 1) : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), BufferBlock>, Uniform>
  spirv.func @copy_via_local() -> () "None" {
    %var = spirv.Variable : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), BufferBlock>, Function>
    %in = spirv.mlir.addressof @indata : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), BufferBlock>, Uniform>
    %out = spirv.mlir.addressof @outdata : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), BufferBlock>, Uniform>
    spirv.CopyMemory "Function" %var, "Uniform" %in : !spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), BufferBlock>
    spirv.CopyMemory "Uniform" %out, "Function" %var : !spirv.struct<(vector<4xf32> [0], vector<4xf32> [16]), BufferBlock>
    spirv.Return
  }
}
