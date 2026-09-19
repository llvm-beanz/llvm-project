// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L124(m): a *whole*-matrix access into a `RowMajor`+
// `MatrixStride`-decorated *non-square* matrix (`mat4x3`: 4 columns, 3
// rows) reached through *two* levels of fixed-size array nesting --
// exactly what `dEQP-VK.ssbo.layout.2_level_array.std430.row_major_mat4x3`
// (and its `3_level_array`/`3_level_unsized_array` siblings) exercise.
// spirv-to-llvm-matrix-rowmajor-nested-array-block.mlir already covers
// this same two-level-nesting shape for a *square* matrix (`mat2x2`),
// where it happens to work correctly even without this fix: a square
// matrix's natural (unpadded, always-column-major) LLVM size and its
// physical (RowMajor-transposed, MatrixStride-padded) size always
// coincide, so the plain, matrix-unaware array padding
// `convertArrayTypeIgnoringDecorations`/MLIR's own upstream array
// conversion fallback already apply happens to reach the right byte size
// by coincidence.
//
// For a *non-square* matrix, that coincidence breaks: `mat4x3`'s natural
// LLVM conversion is `!llvm.array<4 x vector<3xf32>>`, 64 bytes (LLVM
// pads a 3-lane vector's own storage to 16 bytes, not the tightly-packed
// 12), but its correct RowMajor/MatrixStride=16 physical size is only 48
// bytes (`NumRows(3) * MatrixStride(16)`). The inner array's own declared
// `ArrayStride` is 48, matching the *correct* physical size -- but this
// file's own convertArrayTypeIgnoringDecorations correctly declines to
// convert an array whose *natural* element size (64) overshoots that
// declared stride, and that decline falls through to MLIR's own upstream
// `spirv::ArrayType` conversion, which instead validates the stride
// against `VulkanLayoutUtils::getNaturalArrayStride` -- a *different*,
// tightly-packed-scalar-count size for a matrix element (`NumRows *
// NumColumns * sizeof(f32)` = 48, matching the declared stride here) --
// and then silently builds the array around the *natural* (64-byte,
// wrong) matrix conversion anyway, corrupting every inner-array index's
// own byte address by an extra 16 bytes per step.
//
// Fixed by substituteArrayOfMatrixElementType, called from
// rewriteBlockAccess wherever an array-wrapped matrix's own plain
// `Converter.convertType` result would otherwise be used directly (both
// before, and inside, its own further-level-peeling loop): whenever the
// matrix nested inside is not representable in that plain conversion
// (any `RowMajor` matrix, or one whose `MatrixStride` does not match its
// own natural per-column/per-row size), it is substituted with the same
// physical (`getPhysicalMatrixMemberType`/`wrapPhysicalMatrixInArrays`)
// layout convertOffsetStructTypeIgnoringDecorations's own struct-member
// conversion already uses instead.

// CHECK-LABEL: llvm.func @read_whole
// CHECK-SAME: (%[[OUTER:.*]]: i32, %[[INNER:.*]]: i32) -> !llvm.array<4 x vector<3xf32>>
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.array<4 x array<3 x array<4 x vector<3xf32>>>>, 12, 1, 144>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %[[OUTER]])
// CHECK-SAME: -> !llvm.ptr<11>
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[PTR]][0, %[[INNER]]]
// CHECK-SAME: !llvm.ptr<11>, !llvm.array<3 x array<3 x array<4 x f32>>>
// CHECK: %[[LOADED:.*]] = llvm.load %[[ELEM]] : !llvm.ptr<11> -> !llvm.array<3 x array<4 x f32>>
// CHECK: llvm.return %{{.*}} : !llvm.array<4 x vector<3xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @Buf bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.array<4 x !spirv.array<3 x !spirv.matrix<4 x vector<3xf32>>, stride=48>, stride=144> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>
  spirv.func @read_whole(%outer : si32, %inner : si32) -> !spirv.matrix<4 x vector<3xf32>> "None" {
    %0 = spirv.mlir.addressof @Buf : !spirv.ptr<!spirv.struct<(!spirv.array<4 x !spirv.array<3 x !spirv.matrix<4 x vector<3xf32>>, stride=48>, stride=144> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : si32
    %ac = spirv.AccessChain %0[%c0, %outer, %inner] : !spirv.ptr<!spirv.struct<(!spirv.array<4 x !spirv.array<3 x !spirv.matrix<4 x vector<3xf32>>, stride=48>, stride=144> [0, RowMajor, MatrixStride=16]), Block>, StorageBuffer>, si32, si32, si32 -> !spirv.ptr<!spirv.matrix<4 x vector<3xf32>>, StorageBuffer>
    %v = spirv.Load "StorageBuffer" %ac : !spirv.matrix<4 x vector<3xf32>>
    spirv.ReturnValue %v : !spirv.matrix<4 x vector<3xf32>>
  }
  spirv.EntryPoint "GLCompute" @read_whole
  spirv.ExecutionMode @read_whole "LocalSize", 1, 1, 1
}
