// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L124(k): a *whole*-matrix access into a `RowMajor`+
// `MatrixStride`-decorated matrix member of an *arrayed block instance*
// (GLSL `buffer Block { mat2 var; } block[3];` -- a single binding
// covering 3 descriptors, each its own storage buffer block instance,
// unlike every other L124 fix's own shape of an array *member nested
// inside* one block) -- exactly what
// `dEQP-VK.ssbo.layout.instance_array_basic_type.std140.row_major_mat2`
// (and its every other matrix-typed sibling, 84 cases total) exercises.
//
// getMatrixWholeAccess/getMatrixColumnAccess re-derive their own shape
// directly from a `spirv.AccessChain`'s original (unconverted) base
// pointer type. For a plain (non-arrayed) block, that pointer's pointee
// is directly the block's own `spirv::StructType`, exactly what both
// helpers assumed. For an arrayed block instance, that pointer's pointee
// is instead an `spirv::ArrayType` *of* that struct (one level per
// instance-array dimension) -- neither helper accounted for this extra
// wrapping level, so `getBufferBlockElement`/`getUniformBlockElement`
// (which both require a `StructType` pointee directly) always returned
// `std::nullopt`, and every RowMajor/non-representable matrix reached
// this way silently fell back to the generic, physically-wrong (always
// natural, always-column-major) `spirv.Store`/`spirv.Load` conversion --
// note this is a *silent* miscompile, not a legalization failure, unlike
// several other L124 sub-items.
//
// (`ArrayedBlockAccessChainPattern`, the pattern that actually builds the
// per-instance handle and then delegates to `rewriteBlockAccess` for the
// rest of the navigation, already correctly accounted for the leading
// instance-selecting index via its own `Selector` parameter -- this bug
// was isolated to the two matrix-specific helpers above, which
// independently re-derive their own shape straight from the SPIR-V
// `AccessChainOp` rather than reusing `rewriteBlockAccess`'s own
// resolved `Element`/`Selector`.)
//
// Fixed by peelInstanceArrayPointer, called from both helpers to peel
// however many leading array levels wrap the per-instance struct before
// applying their own pre-existing per-block shape logic unchanged (any
// leading array levels found simply add that many to every index
// position/count check already in place).

// A `RowMajor` `mat2` member of an arrayed (`buffer Block { ... } block[3]`)
// storage buffer block: the matrix's own logical (natural, column-major)
// value ((%arg1)) must be transposed and MatrixStride-padded into the
// physical struct<packed(array<2xf32>, array<8xi8>)> layout, not stored
// as-is. This block uses the pre-SPIR-V-1.3 `Uniform`-storage-class +
// `BufferBlock`-decoration spelling deliberately (rather than the more
// common `StorageBuffer`/`Block` one) -- the handle's own storage-class
// integer parameter (`12`) is `convertBufferBlockType`'s own canonical
// "this is a storage buffer" marker (roadmap L124(l)), not the pointer's
// literal SPIR-V storage class value (`2`, `Uniform`).

// CHECK-LABEL: llvm.func @store_whole
// CHECK-SAME: (%[[IDX:.*]]: i32, %[[M:.*]]: !llvm.array<2 x vector<2xf32>>)
// CHECK: %[[COUNT:.*]] = llvm.mlir.constant(3 : i32) : i32
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: %[[COUNT]], %[[IDX]]
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>)>, 12, 1>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK-SAME: -> !llvm.ptr<12>
// CHECK: llvm.store %{{.*}}, %[[PTR]] : !llvm.array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>, !llvm.ptr<12>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @blocks bind(0, 1) : !spirv.ptr<!spirv.array<3 x !spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=16]), BufferBlock>>, Uniform>
  spirv.func @store_whole(%idx : i32, %m : !spirv.matrix<2 x vector<2xf32>>) -> () "None" {
    %0 = spirv.mlir.addressof @blocks : !spirv.ptr<!spirv.array<3 x !spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=16]), BufferBlock>>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%idx, %c0] : !spirv.ptr<!spirv.array<3 x !spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=16]), BufferBlock>>, Uniform>, i32, i32 -> !spirv.ptr<!spirv.matrix<2 x vector<2xf32>>, Uniform>
    spirv.Store "Uniform" %ac, %m : !spirv.matrix<2 x vector<2xf32>>
    spirv.Return
  }
}

// -----

// The load-side counterpart, also confirming the column-select
// (`RowMajor` gather) path getMatrixColumnAccess covers works for the
// same arrayed shape: `block[idx].var[col]`.

// CHECK-LABEL: llvm.func @load_column
// CHECK-SAME: (%[[IDX:.*]]: i32, %[[COL:.*]]: i32) -> vector<2xf32>
// CHECK: %[[COUNT:.*]] = llvm.mlir.constant(3 : i32) : i32
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: %[[COUNT]], %[[IDX]]
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>)>, 12, 1>
// CHECK: %[[BASE:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK-SAME: -> !llvm.ptr<12>
// CHECK: %[[ROW0:.*]] = llvm.getelementptr inbounds %[[BASE]][0, 0, 0, %[[COL]]]
// CHECK: %[[E0:.*]] = llvm.load %[[ROW0]] : !llvm.ptr<12> -> f32
// CHECK: llvm.insertelement %[[E0]]
// CHECK: %[[ROW1:.*]] = llvm.getelementptr inbounds %[[BASE]][0, 1, 0, %[[COL]]]
// CHECK: %[[E1:.*]] = llvm.load %[[ROW1]] : !llvm.ptr<12> -> f32
// CHECK: llvm.insertelement %[[E1]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @blocks bind(0, 1) : !spirv.ptr<!spirv.array<3 x !spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=16]), BufferBlock>>, Uniform>
  spirv.func @load_column(%idx : i32, %col : i32) -> vector<2xf32> "None" {
    %0 = spirv.mlir.addressof @blocks : !spirv.ptr<!spirv.array<3 x !spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=16]), BufferBlock>>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%idx, %c0, %col] : !spirv.ptr<!spirv.array<3 x !spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=16]), BufferBlock>>, Uniform>, i32, i32, i32 -> !spirv.ptr<vector<2xf32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<2xf32>
    spirv.ReturnValue %v : vector<2xf32>
  }
}
