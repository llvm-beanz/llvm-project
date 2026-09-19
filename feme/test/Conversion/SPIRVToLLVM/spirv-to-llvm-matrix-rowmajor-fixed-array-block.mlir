// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L124(g): a `RowMajor`+`MatrixStride`-decorated matrix reached as
// an element of a `StorageBuffer` block's own sole *fixed-size* (not
// runtime) array member -- the plain GLSL shape
// `buffer Block { mat2 matrices[3]; }` compiles to, exactly what
// `dEQP-VK.ssbo.layout.single_basic_array.std140.row_major_mat2` exercises
// -- used to silently miscompile in two independent ways this file's own
// CHECK lines below confirm are now fixed:
//
// 1. `getBufferBlockElement` only ever recognized FeMe's own dxc-wrapper
//    shape (a sole `spirv.rtarray` member, `HasWrapper=true`) as
//    dynamically-indexed content; a sole *fixed*-size `spirv.array`
//    member (this GLSL shape) fell through to the "ordinary struct"
//    branch (`HasWrapper=false`) instead, exactly like
//    `getUniformBlockElement` already special-cased for a uniform
//    block's own analogous shape (roadmap F12a) but its storage-buffer
//    counterpart never did. This alone meant `getMatrixWholeAccess` (used
//    by RowMajorMatrixStorePattern/RowMajorMatrixLoadPattern) could never
//    recognize *any* whole-matrix access into this shape, no matter what
//    the struct's own converted member type looked like: it fell through
//    to the ordinary, wrong (always-logical/column-major) `spirv.Store`/
//    `spirv.Load` conversion instead, unconditionally silently
//    corrupting the physical RowMajor/MatrixStride layout on every real
//    access. Now fixed by also recognizing a sole fixed-size `spirv.array`
//    member as `HasWrapper=true`, mirroring `getUniformBlockElement`'s own
//    fix exactly.
// 2. Independently, `isMatrixMemberLayoutRepresentable` -- the check
//    `convertOffsetStructTypeIgnoringDecorations`'s own per-member loop
//    uses to decide whether a member needs the physical (RowMajor/
//    MatrixStride-aware) layout substitution at all -- only ever
//    recognized a matrix that is *directly* a struct member's own type;
//    for an array-of-matrices member (this shape, one array level
//    removed) it always answered "representable" (no substitution
//    needed) regardless of the member's real decorations, so even once
//    (1) above is fixed, the struct's own converted member type would
//    still have been the plain, unpadded, natural array-of-matrices
//    conversion -- not the physically padded/transposed one
//    `getPhysicalMatrixMemberType` builds. Now fixed by peeling through
//    any array nesting (`peelArraysToMatrixType`) before checking
//    representability directly via `isMatrixLayoutRepresentable`
//    (exactly as `rewriteBlockAccess` already does for its own, narrower
//    purposes), and re-wrapping any substituted physical matrix type in
//    the same array nesting (`wrapPhysicalMatrixInArrays`) afterward.

// CHECK-LABEL: llvm.func @store_load
// CHECK-SAME: (%[[IDX:.*]]: i32, %[[M:.*]]: !llvm.array<2 x vector<2xf32>>) -> !llvm.array<2 x vector<2xf32>>
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<3 x array<32 x i8>>, 12, 1, 32>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %[[IDX]])
// CHECK-SAME: -> !llvm.ptr<11>
//
// The value stored is transposed (RowMajor) and padded (each row's own
// natural 8-byte size padded up to the declared 16-byte MatrixStride)
// into the physical shape before the real `llvm.store`, and the inverse
// happens after the real `llvm.load` -- both crossing exactly the same
// flat, packed, per-row-padded type; every other (arithmetic/return)
// use of the same value keeps the ordinary logical column-major
// `!llvm.array<2 x vector<2xf32>>` shape.
// CHECK: llvm.store %{{.*}}, %[[PTR]] : !llvm.array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>, !llvm.ptr<11>
// CHECK: %[[LOADED:.*]] = llvm.load %[[PTR]] : !llvm.ptr<11> -> !llvm.array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>
// CHECK: llvm.return %{{.*}} : !llvm.array<2 x vector<2xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.array<3 x !spirv.matrix<2 x vector<2xf32>>, stride=32> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>
  spirv.func @store_load(%idx : si32, %m : !spirv.matrix<2 x vector<2xf32>>) -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %addr = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<(!spirv.array<3 x !spirv.matrix<2 x vector<2xf32>>, stride=32> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %elem = spirv.AccessChain %addr[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.array<3 x !spirv.matrix<2 x vector<2xf32>>, stride=32> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>, si32, si32 -> !spirv.ptr<!spirv.matrix<2 x vector<2xf32>>, StorageBuffer>
    spirv.Store "StorageBuffer" %elem, %m : !spirv.matrix<2 x vector<2xf32>>
    %v = spirv.Load "StorageBuffer" %elem : !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %v : !spirv.matrix<2 x vector<2xf32>>
  }
  spirv.EntryPoint "GLCompute" @store_load
  spirv.ExecutionMode @store_load "LocalSize", 1, 1, 1
}
