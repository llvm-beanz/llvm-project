// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap H133 regression test: a `Block`/`Uniform` struct member that is
// itself a further reordered/padded struct -- an `spirv.AccessChain`
// selecting a member of *that* nested struct needs its own declared-index
// selector remapped to a physical index too, not just the outer struct's
// own first selector. Before this fix, `OffsetStructMemberReorderAccess
// ChainPattern` (and `rewriteBlockAccess`'s own fallback GEP path) only
// ever remapped the first struct-member selector past their own scoping
// point, forwarding every *further* index completely unchanged -- wrong
// whenever a member reached by one of those further indices is itself a
// reordered/padded struct (see `remapNestedStructMemberIndices`).
//
// `!inner`'s declared member 3 (a `mat4x2`, `ColMajor`, `MatrixStride=16`,
// widened -- not merely tightened -- since 16 exceeds a `vector<2xf32>`
// column's own 8-byte natural size; see the Roadmap L124(o) note below)
// is preceded by one interior gap of its own (after declared member 2),
// so its own declared index 3 is physical index 4 in a 5-field
// `!llvm.struct`. `!outer`'s declared member 3 (`!inner` itself) is
// preceded by two interior gaps, so it is physical index 5 in `!outer`'s
// own 6-field `!llvm.struct`. These two remapped indices (5 for `!outer`,
// 4 for `!inner`) are deliberately *different* (found via this roadmap
// item's own investigation) -- confirms the fix's own remap is not
// merely "leave the index unchanged and get lucky": each level's remap
// is independently derived from that level's own struct layout.
//
// Roadmap L124(o): `!inner`'s own two matrix members (`mat2x2`/`mat4x2`,
// both `ColMajor`/`MatrixStride=16` over `vector<2xf32>` columns, whose
// own natural size is 8 bytes -- a non-representable layout,
// `isMatrixLayoutRepresentable` rejects any `MatrixStride` that isn't
// exactly that natural size) used to be unconditionally *tightened*
// (not widened) regardless of representability by every code path that
// converts a nested struct's own body -- both `getTightNestedStructType`
// (`!inner`'s own conversion as embedded in `!outer`) and
// `convertOffsetStructTypeIgnoringDecorations`'s own retry tiers
// (`!inner`'s own *standalone* re-conversion, performed fresh by
// `getStructMemberPhysicalIndex` whenever an AccessChain needs to remap
// a further index into it) -- silently dropping the declared padding
// these two members' own real physical layout needs (each column
// widened from 8 to `MatrixStride`'s own 16 bytes). Now widened instead
// (`packed (array<2 x f32>, array<8 x i8>)` per column) by every one of
// those code paths consistently, matching the un-nested (top-level,
// already-correctly-widened) case's own physical shape.
//
// Before this fix's own last piece (making
// `convertOffsetStructTypeIgnoringDecorations`'s own *direct*-matrix-
// member retry case widen, not just its already-fixed array-of-matrix
// sibling case), `!inner`'s *embedded* conversion (via
// `getTightNestedStructType`) and its *standalone* re-conversion (via
// `getStructMemberPhysicalIndex`) silently disagreed on both members'
// own field layout -- one widened, one still tightened -- which for
// *this* test's own particular shape happened to still produce a
// numerically in-bounds (if type-mismatched) GEP, but for a similar
// shape with a *third*, sibling non-representable matrix member in the
// same nested struct instead crashed outright (`'llvm.getelementptr' op
// index N indexing a struct is out of bounds`, see
// spirv-to-llvm-two-nonrepresentable-matrices-nested-struct.mlir's own
// regression test). Widening both matrix members consistently removes
// not only the interior gap `!inner`'s own layout previously needed to
// skip past each matrix member's undersized tight form, but also --
// since the widened `mat4x2` member's own size now exactly reaches its
// declared offset 64 without any padding member in between -- drops
// `!inner`'s own field count from 6 to 5, which is why the second
// `getelementptr`'s own remapped index below is 4 rather than the 5 it
// used to be.
//
// CHECK-LABEL: llvm.func @read_col
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (array<2 x struct<"feme.tight_vector{{[a-zA-Z0-9_.]*}}", (array<3 x f32>)>>, array<8 x i8>, i32, i32, array<8 x i8>, struct<packed (array<2 x struct<packed (array<2 x f32>, array<8 x i8>)>>, struct<"feme.tight_vector{{[a-zA-Z0-9_.]*}}", (array<4 x i32>)>, i32, array<12 x i8>, array<4 x struct<packed (array<2 x f32>, array<8 x i8>)>>)>)>, 2, 0>
// CHECK: %[[OUTER_IDX:.*]] = llvm.mlir.constant(5 : i32) : i32
// CHECK-NEXT: %[[FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %[[OUTER_IDX]])
// CHECK: llvm.getelementptr inbounds %[[FIELD]][0, 4, %{{.*}}]
// CHECK: llvm.load %{{.*}} : !llvm.ptr<12> -> vector<2xf32>
!inner = !spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, ColMajor, MatrixStride=16], vector<4xi32> [32], si32 [48], !spirv.matrix<4 x vector<2xf32>> [64, ColMajor, MatrixStride=16])>
!outer = !spirv.struct<(!spirv.matrix<2 x vector<3xf32>> [0, ColMajor, MatrixStride=16], si32 [32], si32 [36], !inner [48]), Block>
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 2) : !spirv.ptr<!outer, Uniform>
  spirv.func @read_col(%idx : i32) -> vector<2xf32> "None" {
    %0 = spirv.mlir.addressof @cb : !spirv.ptr<!outer, Uniform>
    %c3 = spirv.Constant 3 : i32
    %ac = spirv.AccessChain %0[%c3, %c3, %idx] : !spirv.ptr<!outer, Uniform>, i32, i32, i32 -> !spirv.ptr<vector<2xf32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<2xf32>
    spirv.ReturnValue %v : vector<2xf32>
  }
}
