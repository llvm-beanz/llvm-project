// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap H124b: a `ColMajor`-decorated matrix reached directly as a
// named `cbuffer`/`ConstantBuffer<T>` struct member whose `MatrixStride`
// (16, one full HLSL constant-buffer register) exceeds its natural,
// tightly packed per-column size (3 rows of `f32`, 12 bytes) --
// isMatrixMemberLayoutRepresentable rejects any mismatched
// `MatrixStride` regardless of majorness, which used to make the whole
// containing struct's own type conversion fail outright, cascading into
// every `spirv.AccessChain` into it (this test's single member-selector
// index reaching the matrix in its own entirety included) failing to
// legalize. getPhysicalMatrixMemberType's own padded substitution -- a
// packed struct of the natural column array plus a trailing byte-array
// pad, repeated once per column -- now represents this exactly.

// CHECK-LABEL: llvm.func @store_load
// CHECK-SAME: (%[[M:.*]]: !llvm.array<2 x vector<3xf32>>) -> !llvm.array<2 x vector<3xf32>>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK-SAME: -> !llvm.ptr<12>
// CHECK: llvm.store %{{.*}}, %[[PTR]] : !llvm.array<2 x struct<packed (array<3 x f32>, array<4 x i8>)>>, !llvm.ptr<12>
// CHECK: %[[LOADED:.*]] = llvm.load %[[PTR]] : !llvm.ptr<12> -> !llvm.array<2 x struct<packed (array<3 x f32>, array<4 x i8>)>>
// CHECK: llvm.return %{{.*}} : !llvm.array<2 x vector<3xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.matrix<2 x vector<3xf32>> [0, ColMajor, MatrixStride=16]), Block>, Uniform>
  spirv.func @store_load(%m : !spirv.matrix<2 x vector<3xf32>>) -> !spirv.matrix<2 x vector<3xf32>> "None" {
    %addr = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(!spirv.matrix<2 x vector<3xf32>> [0, ColMajor, MatrixStride=16]), Block>, Uniform>
    %c0 = spirv.Constant 0 : si32
    %elem = spirv.AccessChain %addr[%c0] : !spirv.ptr<!spirv.struct<(!spirv.matrix<2 x vector<3xf32>> [0, ColMajor, MatrixStride=16]), Block>, Uniform>, si32 -> !spirv.ptr<!spirv.matrix<2 x vector<3xf32>>, Uniform>
    spirv.Store "Uniform" %elem, %m : !spirv.matrix<2 x vector<3xf32>>
    %v = spirv.Load "Uniform" %elem : !spirv.matrix<2 x vector<3xf32>>
    spirv.ReturnValue %v : !spirv.matrix<2 x vector<3xf32>>
  }
  spirv.EntryPoint "GLCompute" @store_load
  spirv.ExecutionMode @store_load "LocalSize", 1, 1, 1
}
