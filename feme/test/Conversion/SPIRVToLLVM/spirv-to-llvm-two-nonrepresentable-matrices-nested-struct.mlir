// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L124(o) regression test: a nested struct member (`!sC`, itself
// nested inside `!sD`, itself a member of the top-level `Block`,
// matching `dEQP-VK.ubo.random.nested_structs_arrays_instance_arrays_
// compute.4`'s own randomized struct-shape fuzzer case) that contains
// *two* non-representable (`ColMajor`/`MatrixStride=16` over
// `vector<2xf32>` columns, whose own natural size is 8 bytes) matrix
// members used to crash (`'llvm.getelementptr' op index N indexing a
// struct is out of bounds`) rather than merely mis-widen, unlike this
// same roadmap item's other, single-non-representable-matrix nested-
// struct regression tests.
//
// Root cause: `convertOffsetStructTypeIgnoringDecorations`'s own
// `WithArraysAndMatrices` retry tier -- reached whenever a struct's
// "natural" per-member conversion doesn't reproduce every declared
// offset simultaneously, which `!sC`'s own standalone conversion (as
// happens when `getStructMemberPhysicalIndex` re-derives `!sC`'s own
// physical layout fresh, rather than reusing whatever layout `!sD`'s own
// embedding conversion already produced for its `!sC` member) does *not*
// do here, since the interior gap `!sC`'s own second matrix member's
// declared offset needs isn't reproduced by the natural tier's own
// no-interior-pad layout attempt -- had its own separate, direct-matrix-
// member case (distinct from this same tier's array-of-matrix case,
// already fixed earlier in this same roadmap item) that unconditionally
// *tightened* (never widened) a direct matrix member, exactly the same
// bug class this whole roadmap item fixes, just in a third location.
// Tightening `!sC`'s own second matrix member here (rather than
// widening it, as `!sD`'s own embedding conversion of `!sC` -- reached
// via `getTightNestedStructType`, already fixed -- correctly does)
// produced a `!sC` with a *different* field layout than the one
// `getStructMemberPhysicalIndex`'s caller (`remapNestedStructMemberIndices`)
// actually needs to index into, causing the physical index computed for
// `!sC`'s own declared member 3 to run off the end of the (differently-
// shaped, tightened) struct `WithArraysAndMatrices`'s own retry produced.
//
// Fixed by giving this tier's own direct-matrix-member case the same
// widen-or-tighten treatment (`getTightOrPhysicalMatrixMemberType`) its
// sibling array-of-matrix case already has, so every code path that
// converts `!sC`'s own layout -- standalone or embedded -- agrees on the
// same (correctly widened) shape.

!sC = !spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, ColMajor, MatrixStride=16], vector<4xi32> [32], si32 [48], !spirv.matrix<4 x vector<2xf32>> [64, ColMajor, MatrixStride=16])>
!sD = !spirv.struct<(!spirv.matrix<2 x vector<3xf32>> [0, ColMajor, MatrixStride=16], si32 [32], si32 [36], !sC [48])>

// CHECK-LABEL: llvm.func @read
// CHECK: %[[SD_IDX:.*]] = llvm.mlir.constant(2 : i32) : i32
// CHECK-NEXT: %[[SD_FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%{{.*}}, %[[SD_IDX]])
// CHECK: llvm.getelementptr inbounds %[[SD_FIELD]][0, 5, 4]
// CHECK: llvm.load %{{.*}} : !llvm.ptr<12> -> !llvm.array<4 x struct<packed (array<2 x f32>, array<8 x i8>)>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 0) : !spirv.ptr<!spirv.struct<(si32 [0], !sD [16], f32 [192]), Block>, Uniform>
  spirv.func @read() -> !spirv.matrix<4 x vector<2xf32>> "None" {
    %addr = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(si32 [0], !sD [16], f32 [192]), Block>, Uniform>
    %c1 = spirv.Constant 1 : si32
    %c3 = spirv.Constant 3 : si32
    %ac = spirv.AccessChain %addr[%c1, %c3, %c3] : !spirv.ptr<!spirv.struct<(si32 [0], !sD [16], f32 [192]), Block>, Uniform>, si32, si32, si32 -> !spirv.ptr<!spirv.matrix<4 x vector<2xf32>>, Uniform>
    %v = spirv.Load "Uniform" %ac : !spirv.matrix<4 x vector<2xf32>>
    spirv.ReturnValue %v : !spirv.matrix<4 x vector<2xf32>>
  }
  spirv.EntryPoint "GLCompute" @read
  spirv.ExecutionMode @read "LocalSize", 1, 1, 1
}
