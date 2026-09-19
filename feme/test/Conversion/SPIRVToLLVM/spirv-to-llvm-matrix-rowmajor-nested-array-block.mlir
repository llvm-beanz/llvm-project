// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L124(i): the same `RowMajor`+`MatrixStride`-decorated matrix
// shape spirv-to-llvm-matrix-rowmajor-fixed-array-block.mlir already
// covers (roadmap L124(g)), but reached through *two* levels of
// fixed-size array nesting instead of one -- the plain GLSL shape
// `buffer Block { mat2 matrices[3][2]; }` compiles to, exactly what
// `dEQP-VK.ssbo.layout.2_level_array.std140.column_major_mat2` (and its
// `row_major`/`3_level_array` siblings) exercise.
//
// `getMatrixWholeAccess` (used by RowMajorMatrixStorePattern/
// RowMajorMatrixLoadPattern to recognize a whole-matrix `spirv.Store`/
// `spirv.Load` and reinterpret it through the matrix's own physical,
// RowMajor/MatrixStride-aware layout) used to hard-code its wrapper-shape
// branch to accept only *exactly* two `spirv.AccessChain` indices (the
// wrapper struct's own dummy selector plus a single array index) --
// exactly L124(g)'s own one-level-of-nesting shape, and no other. A
// second array level (as here) pushes the real index count to three,
// which fell outside that hard-coded check entirely, so
// `getMatrixWholeAccess` always returned `std::nullopt` for this shape:
// neither store nor load pattern ever fired, and the plain, wrong
// (always-logical/natural, unpadded) generic `spirv.Store`/`spirv.Load`
// conversion silently took over instead, corrupting the physical
// RowMajor/MatrixStride layout on every real access. Now fixed by
// replacing that hard-coded index count with a loop that peels through
// however many levels of array nesting the wrapper's own sole member
// actually has before reaching the matrix, and requiring exactly that
// many `spirv.AccessChain` indices instead of always exactly two.
//
// (The array levels' own `ArrayStride` decorations were already handled
// correctly by the ordinary, pre-existing array type conversion --
// `convertArrayTypeIgnoringDecorations`'s own undersized-element
// byte-array substitution already pads each array level up to its own
// declared stride regardless of how many levels deep a matrix element
// sits, so no further change was needed there; only the whole-matrix
// *access recognition* itself was too narrow.)

// CHECK-LABEL: llvm.func @store_load
// CHECK-SAME: (%[[OUTER:.*]]: i32, %[[INNER:.*]]: i32, %[[M:.*]]: !llvm.array<2 x vector<2xf32>>) -> !llvm.array<2 x vector<2xf32>>
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<3 x array<2 x array<32 x i8>>>, 12, 1, 64>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %[[OUTER]])
// CHECK-SAME: -> !llvm.ptr<11>
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[PTR]][0, %[[INNER]]]
// CHECK-SAME: !llvm.ptr<11>, !llvm.array<2 x array<32 x i8>>
//
// The value stored is transposed (RowMajor) and padded (each row's own
// natural 8-byte size padded up to the declared 16-byte MatrixStride)
// into the physical shape before the real `llvm.store`, and the inverse
// happens after the real `llvm.load` -- both crossing exactly the same
// flat, packed, per-row-padded type reached by `%[[ELEM]]`; every other
// (arithmetic/return) use of the same value keeps the ordinary logical
// column-major `!llvm.array<2 x vector<2xf32>>` shape.
// CHECK: llvm.store %{{.*}}, %[[ELEM]] : !llvm.array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>, !llvm.ptr<11>
// CHECK: %[[LOADED:.*]] = llvm.load %[[ELEM]] : !llvm.ptr<11> -> !llvm.array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>
// CHECK: llvm.return %{{.*}} : !llvm.array<2 x vector<2xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.array<3 x !spirv.array<2 x !spirv.matrix<2 x vector<2xf32>>, stride=32>, stride=64> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>
  spirv.func @store_load(%outer : si32, %inner : si32, %m : !spirv.matrix<2 x vector<2xf32>>) -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %addr = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<(!spirv.array<3 x !spirv.array<2 x !spirv.matrix<2 x vector<2xf32>>, stride=32>, stride=64> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %elem = spirv.AccessChain %addr[%c0, %outer, %inner] : !spirv.ptr<!spirv.struct<(!spirv.array<3 x !spirv.array<2 x !spirv.matrix<2 x vector<2xf32>>, stride=32>, stride=64> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>, si32, si32, si32 -> !spirv.ptr<!spirv.matrix<2 x vector<2xf32>>, StorageBuffer>
    spirv.Store "StorageBuffer" %elem, %m : !spirv.matrix<2 x vector<2xf32>>
    %v = spirv.Load "StorageBuffer" %elem : !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %v : !spirv.matrix<2 x vector<2xf32>>
  }
  spirv.EntryPoint "GLCompute" @store_load
  spirv.ExecutionMode @store_load "LocalSize", 1, 1, 1
}
