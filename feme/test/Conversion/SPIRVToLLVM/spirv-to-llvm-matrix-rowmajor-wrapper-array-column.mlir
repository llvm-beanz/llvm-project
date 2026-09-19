// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L124(f): a column-select `spirv.AccessChain` into a `RowMajor`
// matrix reached through a dxc wrapper's own array
// (`RWStructuredBuffer<matCxR>`), one or more levels deep -- the shape
// `dEQP-VK.ssbo.layout.single_basic_array.std140.row_major_mat2_store_cols`
// and its `2_level_array`/`3_level_array`/`3_level_unsized_array` siblings
// exercise. Previously declined outright (see this file's own history in
// spirv-to-llvm-matrix-block-invalid.mlir, whose second `RUN` split this
// case used to live in before this fix): `getMatrixColumnAccessShape`
// only ever recognized `Element.Content` directly a struct, or an array
// of struct -- never an array (at any nesting depth) directly wrapping
// the matrix itself, with no intervening struct at all. Fixed by adding
// `getWrapperArrayMatrixColumnAccess`, which peels `Element.Content`
// through however many array levels precede the matrix (mirroring
// `rewriteBlockAccess`'s own L124(j)-added peel) and reads the
// `RowMajor`/`MatrixStride` decorations off the wrapper struct's own sole
// member (always index 0), rather than a member index read from the
// access chain itself -- there is none to read for this shape.
// `rewriteBlockAccess`'s own RowMajor-column-select branch no longer
// declines when `Element.HasWrapper` is true, since `ElementPtr` already
// points at the matrix's own base address regardless of wrapper/nesting,
// exactly like the direct (non-wrapper) shape it already deferred for.

// CHECK-LABEL: llvm.func @read_column
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[ROW0:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 0, %{{.*}}]
// CHECK: llvm.load %[[ROW0]] : !llvm.ptr<11> -> f32
// CHECK: %[[ROW1:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 1, %{{.*}}]
// CHECK: llvm.load %[[ROW1]] : !llvm.ptr<11> -> f32
// CHECK: %[[ROW2:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 2, %{{.*}}]
// CHECK: llvm.load %[[ROW2]] : !llvm.ptr<11> -> f32
// CHECK: %[[ROW3:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 3, %{{.*}}]
// CHECK: llvm.load %[[ROW3]] : !llvm.ptr<11> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 5) : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.mat4v4float, (!spirv.rtarray<!spirv.matrix<4 x vector<4xf32>>, stride=64> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>
  spirv.func @read_column(%idx : si32, %col : si32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.mat4v4float, (!spirv.rtarray<!spirv.matrix<4 x vector<4xf32>>, stride=64> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %ac = spirv.AccessChain %0[%c0, %idx, %col] : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.mat4v4float, (!spirv.rtarray<!spirv.matrix<4 x vector<4xf32>>, stride=64> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>, si32, si32, si32 -> !spirv.ptr<vector<4xf32>, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : vector<4xf32>
    spirv.ReturnValue %v : vector<4xf32>
  }
}

// -----

// The same shape, but reached through *two* levels of fixed-size array
// nesting instead of one wrapper `rtarray` level -- the plain GLSL shape
// `buffer Block { mat2 matrices[4][3]; }` compiles to, exactly what
// `dEQP-VK.ssbo.layout.2_level_array.std140.row_major_mat2_store_cols`
// exercises.

// CHECK-LABEL: llvm.func @read_column_nested
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[MEMBER:.*]] = llvm.getelementptr inbounds %[[PTR]][0, %{{.*}}]
// CHECK: %[[ROW0:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 0, 0, %{{.*}}]
// CHECK: llvm.load %[[ROW0]] : !llvm.ptr<11> -> f32
// CHECK: %[[ROW1:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 1, 0, %{{.*}}]
// CHECK: llvm.load %[[ROW1]] : !llvm.ptr<11> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 5) : !spirv.ptr<!spirv.struct<(!spirv.array<4 x !spirv.array<3 x !spirv.matrix<2 x vector<2xf32>>, stride=32>, stride=96> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>
  spirv.func @read_column_nested(%outer : si32, %inner : si32, %col : si32) -> vector<2xf32> "None" {
    %0 = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<(!spirv.array<4 x !spirv.array<3 x !spirv.matrix<2 x vector<2xf32>>, stride=32>, stride=96> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %ac = spirv.AccessChain %0[%c0, %outer, %inner, %col] : !spirv.ptr<!spirv.struct<(!spirv.array<4 x !spirv.array<3 x !spirv.matrix<2 x vector<2xf32>>, stride=32>, stride=96> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>, si32, si32, si32, si32 -> !spirv.ptr<vector<2xf32>, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : vector<2xf32>
    spirv.ReturnValue %v : vector<2xf32>
  }
}
