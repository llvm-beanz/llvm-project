// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// (Roadmap H132) Column-select and scalar-element access into a
// non-representable-layout matrix reached through a dxc wrapper's own
// array (`RWStructuredBuffer<matCxR>`/`StructuredBuffer<matCxR>`, unlike
// spirv-to-llvm-matrix-block-column.mlir/-scalar.mlir's own *direct*
// struct-member matrix) now works for the `ColMajor`-padded case exactly
// like the direct shape does: `rewriteBlockAccess`'s own
// `ElementPtr` -- the matrix's own base address -- is already correct
// for either shape (a `spirv.resource.getpointer` selecting the wrapper's
// array element, or the struct member directly), so the same
// `member_base + col * MatrixStride` GEP applies unchanged. Before this
// fix, `isMatrixMemberLayoutRepresentable` only ever examined whether
// `Struct.getElementType(Index)` -- the wrapper's own sole member, an
// array of the matrix, not the matrix itself -- was directly a matrix,
// always answering "representable" for this shape regardless of its
// real decorations, so this access silently fell through to the ordinary
// (wrong, natural-layout) GEP instead.

// CHECK-LABEL: llvm.func @read_column
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[COL:.*]] = llvm.getelementptr inbounds %[[MEMBER]][%{{.*}}]
// CHECK-SAME: !llvm.array<48 x i8>
// CHECK: llvm.load %[[COL]] : !llvm.ptr<11> -> vector<4xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 5) : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.mat4v4float, (!spirv.rtarray<!spirv.matrix<4 x vector<4xf32>>, stride=192> [0, ColMajor, MatrixStride=48]), Block>, StorageBuffer>
  spirv.func @read_column(%idx : si32, %col : si32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.mat4v4float, (!spirv.rtarray<!spirv.matrix<4 x vector<4xf32>>, stride=192> [0, ColMajor, MatrixStride=48]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %ac = spirv.AccessChain %0[%c0, %idx, %col] : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.mat4v4float, (!spirv.rtarray<!spirv.matrix<4 x vector<4xf32>>, stride=192> [0, ColMajor, MatrixStride=48]), Block>, StorageBuffer>, si32, si32, si32 -> !spirv.ptr<vector<4xf32>, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : vector<4xf32>
    spirv.ReturnValue %v : vector<4xf32>
  }
}

// -----

// The scalar-element counterpart: `matrix[col][row]`, a further scalar
// index past the column-select shape above.

// CHECK-LABEL: llvm.func @read_scalar
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, %{{.*}}, 0, %{{.*}}]
// CHECK: llvm.load %[[ELEM]] : !llvm.ptr<11> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 5) : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.mat4v4float, (!spirv.rtarray<!spirv.matrix<4 x vector<4xf32>>, stride=192> [0, ColMajor, MatrixStride=48]), Block>, StorageBuffer>
  spirv.func @read_scalar(%idx : si32, %col : si32, %row : si32) -> f32 "None" {
    %0 = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.mat4v4float, (!spirv.rtarray<!spirv.matrix<4 x vector<4xf32>>, stride=192> [0, ColMajor, MatrixStride=48]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %ac = spirv.AccessChain %0[%c0, %idx, %col, %row] : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.mat4v4float, (!spirv.rtarray<!spirv.matrix<4 x vector<4xf32>>, stride=192> [0, ColMajor, MatrixStride=48]), Block>, StorageBuffer>, si32, si32, si32, si32 -> !spirv.ptr<f32, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : f32
    spirv.ReturnValue %v : f32
  }
}
