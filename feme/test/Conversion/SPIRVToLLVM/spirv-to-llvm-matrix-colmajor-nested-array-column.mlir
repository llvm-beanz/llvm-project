// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L124(j): column-select and scalar-element access into a
// `ColMajor`+`MatrixStride`-decorated matrix reached through *two*
// levels of array nesting -- one level deeper than
// spirv-to-llvm-matrix-block-wrapper-partial.mlir's own single-level
// wrapper-array-of-matrix-directly shape, and exactly what
// `dEQP-VK.ssbo.layout.2_level_array.std140.column_major_mat2_store_cols`
// (and its `3_level_array` siblings) exercise.
//
// `rewriteBlockAccess`'s own `isa<MatrixType>(SelectedType)` partial-
// access branch used to only ever fire when `SelectedType` (the type
// reached one array/struct-member index past the wrapper's initial
// selector) was *directly* a matrix -- for any further array nesting,
// `SelectedType` was still an array at that point, so the branch never
// fired and the access silently fell through to the generic,
// `MatrixStride`-unaware index-forwarding GEP fallback instead (which
// computes a plain, physically-wrong byte offset). Fixed by peeking
// ahead through however many further array levels precede the matrix
// (mirroring L124(i)'s `getMatrixWholeAccess` generalization) before
// deciding whether the matrix-specific column/scalar logic applies, and
// generalizing the branch's own `Selector+1`/`+2`/`+3` index-position
// arithmetic to account for however many levels were peeled.

// CHECK-LABEL: llvm.func @read_column
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[OUTER:.*]] = llvm.getelementptr inbounds %[[PTR]][0, %{{.*}}]
// CHECK: %[[COL:.*]] = llvm.getelementptr inbounds %[[OUTER]][%{{.*}}]
// CHECK-SAME: !llvm.array<16 x i8>
// CHECK: llvm.load %[[COL]] : !llvm.ptr<11> -> vector<2xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 5) : !spirv.ptr<!spirv.struct<(!spirv.array<4 x !spirv.array<3 x !spirv.matrix<2 x vector<2xf32>>, stride=32>, stride=96> [0, ColMajor, MatrixStride=16]), Block>, StorageBuffer>
  spirv.func @read_column(%outer : si32, %inner : si32, %col : si32) -> vector<2xf32> "None" {
    %0 = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<(!spirv.array<4 x !spirv.array<3 x !spirv.matrix<2 x vector<2xf32>>, stride=32>, stride=96> [0, ColMajor, MatrixStride=16]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %ac = spirv.AccessChain %0[%c0, %outer, %inner, %col] : !spirv.ptr<!spirv.struct<(!spirv.array<4 x !spirv.array<3 x !spirv.matrix<2 x vector<2xf32>>, stride=32>, stride=96> [0, ColMajor, MatrixStride=16]), Block>, StorageBuffer>, si32, si32, si32, si32 -> !spirv.ptr<vector<2xf32>, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : vector<2xf32>
    spirv.ReturnValue %v : vector<2xf32>
  }
}

// -----

// The scalar-element counterpart: `matrices[outer][inner][col][row]`, a
// further scalar index past the column-select shape above.

// CHECK-LABEL: llvm.func @read_scalar
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[OUTER:.*]] = llvm.getelementptr inbounds %[[PTR]][0, %{{.*}}]
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[OUTER]][0, %{{.*}}, 0, %{{.*}}]
// CHECK: llvm.load %[[ELEM]] : !llvm.ptr<11> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 5) : !spirv.ptr<!spirv.struct<(!spirv.array<4 x !spirv.array<3 x !spirv.matrix<2 x vector<2xf32>>, stride=32>, stride=96> [0, ColMajor, MatrixStride=16]), Block>, StorageBuffer>
  spirv.func @read_scalar(%outer : si32, %inner : si32, %col : si32, %row : si32) -> f32 "None" {
    %0 = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<(!spirv.array<4 x !spirv.array<3 x !spirv.matrix<2 x vector<2xf32>>, stride=32>, stride=96> [0, ColMajor, MatrixStride=16]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %ac = spirv.AccessChain %0[%c0, %outer, %inner, %col, %row] : !spirv.ptr<!spirv.struct<(!spirv.array<4 x !spirv.array<3 x !spirv.matrix<2 x vector<2xf32>>, stride=32>, stride=96> [0, ColMajor, MatrixStride=16]), Block>, StorageBuffer>, si32, si32, si32, si32, si32 -> !spirv.ptr<f32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : f32
    spirv.ReturnValue %v : f32
  }
}
