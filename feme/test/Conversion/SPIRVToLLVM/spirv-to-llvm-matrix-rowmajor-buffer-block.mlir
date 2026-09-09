// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L83: a `RowMajor`+`MatrixStride`-decorated matrix reached as the
// element of a `RWStructuredBuffer<matCxR>`'s own wrapped runtime array
// (dxc's own single-member wrapper shape, unlike
// spirv-to-llvm-matrix-block(-invalid).mlir's *direct* struct-member
// matrix) is a physically row-major layout that LLVM's own natural
// column-major `!llvm.array<NumColumns x vector<NumRows x T>>`
// representation cannot reproduce by reinterpreting the same bytes --
// `isMatrixMemberLayoutRepresentable` only ever examines a matrix that is
// directly a struct member, so this array-wrapped shape used to fall
// through the ordinary (wrong) `BlockAccessChainPattern` conversion
// entirely unnoticed, silently corrupting every store/load of such a
// matrix instead of failing loudly. `RowMajorMatrixStorePattern`/
// `RowMajorMatrixLoadPattern` (SPIRVToLLVMPatterns.cpp) now transpose the
// value at the exact point it crosses the memory boundary: the store/load
// instruction itself uses the "physical" row-major type, a flat, packed
// `!llvm.array<NumRows x array<NumColumns x T>>` (deliberately arrays of
// scalars, not vectors of `NumColumns` lanes, since a non-power-of-two
// vector's own in-memory size gets padded up to the next power of two by
// this target's data layout, which would silently corrupt this exact
// 48-byte stride) -- while every arithmetic/temporary use of the same
// value elsewhere keeps the ordinary "logical" column-major
// `!llvm.array<3 x vector<4xf32>>` shape (`%arg1`'s own parameter type,
// and this function's own return type, below).

// CHECK-LABEL: llvm.func @store_load
// CHECK-SAME: (%[[IDX:.*]]: i32, %[[M:.*]]: !llvm.array<3 x vector<4xf32>>) -> !llvm.array<3 x vector<4xf32>>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK-SAME: -> !llvm.ptr<11>
//
// The value stored is transposed into the physical (row-major) shape
// before the real `llvm.store`, and transposed back after the real
// `llvm.load` -- both crossing exactly the same flat scalar-array type.
// CHECK: llvm.store %{{.*}}, %[[PTR]] : !llvm.array<4 x array<3 x f32>>, !llvm.ptr<11>
// CHECK: %[[LOADED:.*]] = llvm.load %[[PTR]] : !llvm.ptr<11> -> !llvm.array<4 x array<3 x f32>>
// CHECK: llvm.return %{{.*}} : !llvm.array<3 x vector<4xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 0) : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.mat3v4float, (!spirv.rtarray<!spirv.matrix<3 x vector<4xf32>>, stride=48> [0, RowMajor, MatrixStride=12]), Block>, StorageBuffer>
  spirv.func @store_load(%idx : si32, %m : !spirv.matrix<3 x vector<4xf32>>) -> !spirv.matrix<3 x vector<4xf32>> "None" {
    %addr = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.mat3v4float, (!spirv.rtarray<!spirv.matrix<3 x vector<4xf32>>, stride=48> [0, RowMajor, MatrixStride=12]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %elem = spirv.AccessChain %addr[%c0, %idx] : !spirv.ptr<!spirv.struct<type.RWStructuredBuffer.mat3v4float, (!spirv.rtarray<!spirv.matrix<3 x vector<4xf32>>, stride=48> [0, RowMajor, MatrixStride=12]), Block>, StorageBuffer>, si32, si32 -> !spirv.ptr<!spirv.matrix<3 x vector<4xf32>>, StorageBuffer>
    spirv.Store "StorageBuffer" %elem, %m : !spirv.matrix<3 x vector<4xf32>>
    %v = spirv.Load "StorageBuffer" %elem : !spirv.matrix<3 x vector<4xf32>>
    spirv.ReturnValue %v : !spirv.matrix<3 x vector<4xf32>>
  }
  spirv.EntryPoint "GLCompute" @store_load
  spirv.ExecutionMode @store_load "LocalSize", 1, 1, 1
}
