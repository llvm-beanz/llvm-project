// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// (Roadmap H129) A scalar-element `spirv.AccessChain` (the member
// selector, a column index, and a further row index --
// `matrix[col][row]`, a scalar `T`) into a `RowMajor` matrix member whose
// declared layout isMatrixMemberLayoutRepresentable always rejects.
// Unlike column-select (spirv-to-llvm-matrix-block-column.mlir), which
// for `RowMajor` needs a genuine multi-row gather, one scalar element is
// always exactly one entry of one physical major (row) entry -- directly,
// statically addressable by a single GEP through the same
// `!llvm.array<NumRows x array<NumColumns x T>>` physical substitution
// the whole-matrix load/store patterns already use, with `col` selecting
// the innermost (minor) index and `row` the outer (major) one --
// `spirv.AccessChain`'s own index order is always [col, row] regardless
// of physical majorness, since SPIR-V's own matrix indexing is always
// column-first.

// CHECK-LABEL: llvm.func @read_scalar
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (array<4 x array<4 x f32>>)>, 2, 0>
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[SCALAR:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, %{{.*}}, %{{.*}}]
// CHECK: llvm.load %[[SCALAR]] : !llvm.ptr<12> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 5) : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, Uniform>
  spirv.func @read_scalar(%col : i32, %row : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %col, %row] : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, Uniform>, i32, i32, i32 -> !spirv.ptr<f32, Uniform>
    %v = spirv.Load "Uniform" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

// -----

// The same scalar-element shape, for a `ColMajor` padded-stride member
// (`MatrixStride=48`, exceeding the natural 16-byte, `sizeof(vector<4xf32>)`
// per-column stride) -- here `col` is the outer (major) index and `row`
// the inner (minor) one, the opposite order from the `RowMajor` case
// above, matching how getPhysicalMatrixMemberType's own major/minor
// choice always follows the declared layout's own physical majorness.

// CHECK-LABEL: llvm.func @read_scalar
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (array<4 x struct<packed (array<4 x f32>, array<32 x i8>)>>)>, 2, 0>
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[SCALAR:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, %{{.*}}, 0, %{{.*}}]
// CHECK: llvm.load %[[SCALAR]] : !llvm.ptr<12> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 5) : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, ColMajor, MatrixStride=48]), Block>, Uniform>
  spirv.func @read_scalar(%col : i32, %row : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, ColMajor, MatrixStride=48]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %col, %row] : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, ColMajor, MatrixStride=48]), Block>, Uniform>, i32, i32, i32 -> !spirv.ptr<f32, Uniform>
    %v = spirv.Load "Uniform" %ac : f32
    spirv.ReturnValue %v : f32
  }
}
