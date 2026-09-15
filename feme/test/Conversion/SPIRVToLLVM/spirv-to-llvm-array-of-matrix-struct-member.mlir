// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H134: a fixed-size *array of matrices* (`float4x3 m[5]`) inside
// an explicitly-offset Block/Uniform struct -- found via
// `dEQP-VK.ubo.random.all_shared_buffer.26`, one of dEQP's own randomized
// struct-shape fuzzer cases -- hit a struct-layout conversion gap
// `convertOffsetStructTypeIgnoringDecorations`'s existing "array-or-
// matrix" retry tier (roadmap H101i) did not cover: that tier only ever
// looked one level past a member's own type for a bare `VectorType` --
// direct `spirv.matrix` (whose own column type is the vector) or a
// direct array-of-vectors (`!spirv.array<N x vector<M x T>>`) -- so an
// *array of matrices* (`!spirv.array<N x !spirv.matrix<...>>`, a matrix
// nested one array dimension deeper than either of those two shapes)
// fell through every retry untouched, unconditionally kept at its
// natural (tightly-packed column-major, alignment-driven) conversion,
// which failed to reproduce this member's own declared offset -- and,
// since no later member in the struct could be substituted for either,
// the *whole struct's* conversion returned null, cascading up to
// `convertUniformBlockType`/`convertBufferBlockType` returning null in
// turn, which (roadmap H130's own investigation found this same
// no-good-fallback path) silently produces a raw, unclassifiable
// `ptr addrspace(12)`/`ptr addrspace(11)` handle instead of a real
// `spirv.VulkanBuffer` one.
//
// Fixed by adding a third case to the same retry tier: `getTightMatrixType`
// (factored out of the existing direct-matrix case's own inline logic)
// substitutes a matrix's own tight (alignment-free) form -- an
// `!llvm.array<NumColumns x TightColumn>`, `TightColumn` built by the
// existing `getTightVectorArrayType` -- and this array-of-matrix case
// wraps that in one more outer `!llvm.array<N x TightMatrix>`, matching
// the outer array's own declared element count. `HasVectorMember`'s own
// per-member classification (which gates whether this retry tier is
// even attempted at all) is extended identically, since an array-of-
// matrices member sharing a struct with no *other* vector/matrix/nested-
// struct member would otherwise never reach this tier in the first
// place.

// CHECK-LABEL: llvm.func @read_scalar_member
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (struct<"feme.tight_vector", (array<4 x i32>)>, i32, array<12 x i8>, array<5 x array<4 x struct<"feme.tight_vector.{{[0-9]+}}", (array<3 x f32>)>>>, struct<"feme.tight_vector.{{[0-9]+}}", (array<2 x i32>)>)>, 2, 0>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: llvm.load %[[PTR]] : !llvm.ptr<12> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 5) : !spirv.ptr<!spirv.struct<(vector<4xsi32> [0], si32 [16], !spirv.array<5 x !spirv.matrix<4 x vector<3xf32>>, stride=48> [32, RowMajor, MatrixStride=16], vector<2xsi32> [272]), Block>, Uniform>
  spirv.func @read_scalar_member(%idx : si32) -> si32 "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(vector<4xsi32> [0], si32 [16], !spirv.array<5 x !spirv.matrix<4 x vector<3xf32>>, stride=48> [32, RowMajor, MatrixStride=16], vector<2xsi32> [272]), Block>, Uniform>
    %c1 = spirv.Constant 1 : si32
    %ac = spirv.AccessChain %0[%c1] : !spirv.ptr<!spirv.struct<(vector<4xsi32> [0], si32 [16], !spirv.array<5 x !spirv.matrix<4 x vector<3xf32>>, stride=48> [32, RowMajor, MatrixStride=16], vector<2xsi32> [272]), Block>, Uniform>, si32 -> !spirv.ptr<si32, Uniform>
    %v = spirv.Load "Uniform" %ac : si32
    spirv.ReturnValue %v : si32
  }
}
