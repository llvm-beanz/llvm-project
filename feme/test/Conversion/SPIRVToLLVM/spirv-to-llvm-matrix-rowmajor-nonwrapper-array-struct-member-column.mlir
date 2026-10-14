// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap K6k: one logical *column* (not the whole matrix) of a `RowMajor`
// matrix reached through a non-wrapper storage buffer block member that is
// itself a direct, fixed-size array of matrices -- e.g. plain GLSL
// `buffer Block { float padding; mat2 var[3]; }` -- rather than the block's
// sole member being such an array (already handled via the `HasWrapper`
// shape, see `spirv-to-llvm-matrix-rowmajor-wrapper-array-column.mlir`).
// Found via the real SPIR-V `dEQP-VK.ssbo.layout.single_basic_array.scalar.
// row_major_mat2_store_cols` compiles to (captured with
// `--deqp-log-decompiled-spirv=enable`): a `float` member at index 0
// shifts the matrix-array member to index 1, so the `AccessChain` selecting
// one column is `[member=1, elemIdx, colIdx]` -- one index deeper than
// `getMatrixColumnAccess`'s own `getMatrixColumnAccessShape` helper
// expected (it only recognized `[member, colIdx]`, i.e. the member being
// the matrix directly), so it declined to match, the whole `AccessChain`
// fell through to a different, RowMajor-oblivious store, and `colIdx` was
// silently dropped -- a 62-case `dEQP-VK.ssbo.*` result-comparison
// mismatch (not a legalization failure), confirmed fixed by peeling
// through the member's own array levels (one here, more for
// `2_level_array`/`3_level_array`) before requiring the innermost type be
// the matrix.

// CHECK-LABEL: llvm.func @store_col
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (f32, array<3 x array<2 x array<2 x f32>>>)>, 12, 1>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]]
// CHECK: %[[ELEM:.*]] = llvm.getelementptr inbounds %[[PTR]][0, %{{.*}}]
// CHECK-SAME: -> !llvm.ptr<11>, !llvm.array<3 x array<2 x array<2 x f32>>>
// CHECK: %[[ROW0:.*]] = llvm.extractelement %{{.*}}[%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[GEP0:.*]] = llvm.getelementptr inbounds %[[ELEM]][0, 0, %{{.*}}]
// CHECK: llvm.store %[[ROW0]], %[[GEP0]]
// CHECK: %[[ROW1:.*]] = llvm.extractelement %{{.*}}[%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[GEP1:.*]] = llvm.getelementptr inbounds %[[ELEM]][0, 1, %{{.*}}]
// CHECK: llvm.store %[[ROW1]], %[[GEP1]]
!Blk = !spirv.struct<(f32 [0, Offset=0], !spirv.array<3 x !spirv.matrix<2 x vector<2xf32>>, stride=16> [4, RowMajor, MatrixStride=8]), Block>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @buf bind(0, 1) : !spirv.ptr<!Blk, StorageBuffer>
  spirv.func @store_col(%col : vector<2xf32>, %elemIdx : si32, %colIdx : si32) "None" {
    %addr = spirv.mlir.addressof @buf : !spirv.ptr<!Blk, StorageBuffer>
    %c1 = spirv.Constant 1 : si32
    %ac = spirv.AccessChain %addr[%c1, %elemIdx, %colIdx] : !spirv.ptr<!Blk, StorageBuffer>, si32, si32, si32 -> !spirv.ptr<vector<2xf32>, StorageBuffer>
    spirv.Store "StorageBuffer" %ac, %col : vector<2xf32>
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @store_col
  spirv.ExecutionMode @store_col "LocalSize", 1, 1, 1
}
