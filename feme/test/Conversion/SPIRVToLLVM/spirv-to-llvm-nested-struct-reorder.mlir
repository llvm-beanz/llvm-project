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
// `!inner`'s declared member 3 (a tight-substituted `mat4x2`, `ColMajor`,
// `MatrixStride=16`) is preceded by two interior gaps of its own (after
// declared members 0 and 2), so its own declared index 3 is physical index
// 5 in a 6-field `!llvm.struct`. `!outer`'s declared member 3 (`!inner`
// itself) is likewise preceded by two interior gaps, so it, too, is
// physical index 5 in `!outer`'s own 6-field `!llvm.struct`. Both
// coinciding at 5 is deliberate (found via this roadmap item's own
// investigation) -- confirms the fix's own remap is not merely "leave the
// index unchanged and get lucky": each level's remap is independently
// derived from that level's own struct layout.
//
// CHECK-LABEL: llvm.func @read_col
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(array<2 x struct<"feme.tight_vector{{[a-zA-Z0-9_.]*}}", (array<3 x f32>)>>, array<8 x i8>, i32, i32, array<8 x i8>, struct<(array<2 x struct<"feme.tight_vector{{[a-zA-Z0-9_.]*}}", (array<2 x f32>)>>, array<16 x i8>, struct<"feme.tight_vector{{[a-zA-Z0-9_.]*}}", (array<4 x i32>)>, i32, array<12 x i8>, array<4 x struct<"feme.tight_vector{{[a-zA-Z0-9_.]*}}", (array<2 x f32>)>>)>)>, 2, 0>
// CHECK: %[[OUTER_IDX:.*]] = llvm.mlir.constant(5 : i32) : i32
// CHECK-NEXT: %[[FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %[[OUTER_IDX]])
// CHECK: llvm.getelementptr inbounds %[[FIELD]][0, 5, %{{.*}}]
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
