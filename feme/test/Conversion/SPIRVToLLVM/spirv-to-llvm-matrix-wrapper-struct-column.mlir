// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// (Roadmap H151) Column-select access into a `RowMajor` matrix reached
// through *two* levels of indirection: a dxc wrapper's own runtime array
// (`StructuredBuffer<MatrixStruct>`), each of whose elements is itself a
// struct with the matrix as its sole member
// (`struct MatrixStruct { float4x4 matrixData; }`) -- found reducing
// `WaveOps/WaveReadLaneAt.mtx.test` down to its exact IR shape. This is
// one level deeper than spirv-to-llvm-matrix-block-wrapper-partial.mlir's
// own wrapper-array-of-matrix-directly shape (`RWStructuredBuffer<matCxR>`,
// no intervening struct), and orthogonal to spirv-to-llvm-matrix-
// pushconstant-scalar.mlir's own non-Block, non-wrapper nested-struct
// shape: before this fix, neither `rewriteBlockAccess`'s own top-level
// matrix branch (SelectedType here is the intervening struct, not the
// matrix itself) nor `remapNestedStructMemberIndices`'s own H148-added
// matrix handling (which only recognizes a further *scalar*-element
// access, two indices past the member selector, not a column select's
// one) recognized this shape at all, so it silently fell through to an
// ordinary GEP treating the RowMajor matrix as an untransposed,
// column-major array -- reading/writing the wrong bytes.
//
// CHECK-LABEL: llvm.func @read_column_rowmajor
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK: %[[ELEM:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[MEMBER:.*]] = llvm.getelementptr inbounds %[[ELEM]][0, 0]
// CHECK: %[[ROW0:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 0, %{{.*}}]
// CHECK: llvm.load %[[ROW0]] : !llvm.ptr<11> -> f32
// CHECK: %[[ROW1:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 1, %{{.*}}]
// CHECK: llvm.load %[[ROW1]] : !llvm.ptr<11> -> f32
// CHECK: %[[ROW2:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 2, %{{.*}}]
// CHECK: llvm.load %[[ROW2]] : !llvm.ptr<11> -> f32
// CHECK: %[[ROW3:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 3, %{{.*}}]
// CHECK: llvm.load %[[ROW3]] : !llvm.ptr<11> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 5) : !spirv.ptr<!spirv.struct<type.StructuredBuffer.MatrixStruct, (!spirv.rtarray<!spirv.struct<MatrixStruct, (!spirv.matrix<4 x vector<4xf32>> [0, MatrixStride=16, RowMajor])>, stride=64> [0, NonWritable]), Block>, StorageBuffer>
  spirv.func @read_column_rowmajor(%idx : si32, %col : si32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<type.StructuredBuffer.MatrixStruct, (!spirv.rtarray<!spirv.struct<MatrixStruct, (!spirv.matrix<4 x vector<4xf32>> [0, MatrixStride=16, RowMajor])>, stride=64> [0, NonWritable]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %c0_member = spirv.Constant 0 : si32
    %ac = spirv.AccessChain %0[%c0, %idx, %c0_member, %col] : !spirv.ptr<!spirv.struct<type.StructuredBuffer.MatrixStruct, (!spirv.rtarray<!spirv.struct<MatrixStruct, (!spirv.matrix<4 x vector<4xf32>> [0, MatrixStride=16, RowMajor])>, stride=64> [0, NonWritable]), Block>, StorageBuffer>, si32, si32, si32, si32 -> !spirv.ptr<vector<4xf32>, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : vector<4xf32>
    spirv.ReturnValue %v : vector<4xf32>
  }
}
