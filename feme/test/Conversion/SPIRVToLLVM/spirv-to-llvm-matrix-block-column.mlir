// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// (Roadmap H129) A column-select `spirv.AccessChain` (the member selector
// plus one further, possibly dynamic index selecting one whole logical
// column -- `matrix[col]`, a `vector<NumRows x T>`; SPIR-V's own matrix
// indexing is always column-first regardless of physical RowMajor/
// ColMajor storage) into a `RowMajor` matrix member -- always physically
// transposed from LLVM's own natural array-of-column-vectors
// representation, so isMatrixMemberLayoutRepresentable always rejects it
// (see spirv-to-llvm-matrix-block.mlir's own `ColMajor`, natural-stride
// case for the one shape it accepts directly) -- cannot be read via a
// single address: one logical column is `NumRows` separate scalars, one
// per physical row entry, each `MatrixStride` bytes apart. This gathers
// one scalar per row directly (MatrixColumnLoadPattern), rather than
// loading the whole matrix and only then selecting a column, since the
// column index may be a genuinely dynamic runtime value.

// CHECK-LABEL: llvm.func @read_column
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(array<4 x array<4 x f32>>)>, 2, 0>
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[ROW0:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 0, %{{.*}}]
// CHECK: llvm.load %[[ROW0]] : !llvm.ptr<12> -> f32
// CHECK: %[[ROW1:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 1, %{{.*}}]
// CHECK: llvm.load %[[ROW1]] : !llvm.ptr<12> -> f32
// CHECK: %[[ROW2:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 2, %{{.*}}]
// CHECK: llvm.load %[[ROW2]] : !llvm.ptr<12> -> f32
// CHECK: %[[ROW3:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 3, %{{.*}}]
// CHECK: llvm.load %[[ROW3]] : !llvm.ptr<12> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 5) : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, Uniform>
  spirv.func @read_column(%idx : i32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, Uniform>, i32, i32 -> !spirv.ptr<vector<4xf32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<4xf32>
    spirv.ReturnValue %v : vector<4xf32>
  }
}

// -----

// (Roadmap H129) A column-select access into a `ColMajor` matrix member
// whose `MatrixStride` (48 here) pads beyond its natural per-column
// stride (16 bytes, `sizeof(vector<4xf32>)`) -- unlike the `RowMajor`
// case above, one logical column IS one physical major entry here (a
// single, contiguous, `MatrixStride`-sized block), so
// `member_base + col * MatrixStride` bytes is exactly its address, and an
// ordinary load of that address as `vector<4xf32>` reads it directly --
// this target's own vector ABI size for 2/3/4 lanes never exceeds a real
// `MatrixStride` reservation, so this needs no dedicated Load/Store
// pattern at all (the GEP itself, built directly by rewriteBlockAccess,
// is the whole fix).

// CHECK-LABEL: llvm.func @read_column
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(array<4 x struct<packed (array<4 x f32>, array<32 x i8>)>>)>, 2, 0>
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[COL:.*]] = llvm.getelementptr inbounds %[[MEMBER]][%{{.*}}]
// CHECK-SAME: !llvm.array<48 x i8>
// CHECK: llvm.load %[[COL]] : !llvm.ptr<12> -> vector<4xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 5) : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, ColMajor, MatrixStride=48]), Block>, Uniform>
  spirv.func @read_column(%idx : i32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, ColMajor, MatrixStride=48]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, ColMajor, MatrixStride=48]), Block>, Uniform>, i32, i32 -> !spirv.ptr<vector<4xf32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<4xf32>
    spirv.ReturnValue %v : vector<4xf32>
  }
}

// -----

// (Roadmap H129) A `RowMajor` column-*store* -- the exact inverse of the
// load case above (MatrixColumnStorePattern) -- not currently exercised
// by any real `dEQP-VK.ubo.*` case (`Uniform`/UBO storage is read-only
// from shader code; only `StorageBuffer`/SSBO supports `spirv.Store`),
// but tested here for symmetry/future-proofing.

// CHECK-LABEL: llvm.func @write_column
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: %[[E0:.*]] = llvm.extractelement %{{.*}}[%{{.*}} : i32]
// CHECK: %[[ROW0:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 0, %{{.*}}]
// CHECK: llvm.store %[[E0]], %[[ROW0]]
// CHECK: %[[E1:.*]] = llvm.extractelement %{{.*}}[%{{.*}} : i32]
// CHECK: %[[ROW1:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, 1, %{{.*}}]
// CHECK: llvm.store %[[E1]], %[[ROW1]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ssbo bind(0, 5) : !spirv.ptr<!spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=8]), Block>, StorageBuffer>
  spirv.func @write_column(%idx : i32, %val : vector<2xf32>) -> () "None" {
    %0 = spirv.mlir.addressof @ssbo : !spirv.ptr<!spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=8]), Block>, StorageBuffer>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=8]), Block>, StorageBuffer>, i32, i32 -> !spirv.ptr<vector<2xf32>, StorageBuffer>
    spirv.Store "StorageBuffer" %ac, %val : vector<2xf32>
    spirv.Return
  }
}
