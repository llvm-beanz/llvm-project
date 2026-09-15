// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap H124b: a `RowMajor`-decorated matrix reached directly as a
// named `cbuffer`/`ConstantBuffer<T>` struct member (dxc's own shape for
// this case, unlike spirv-to-llvm-matrix-rowmajor-buffer-block.mlir's
// dynamically-indexed wrapper-array element) used to make the *whole*
// containing struct's own type conversion fail outright
// (isMatrixMemberLayoutRepresentable rejects any `RowMajor` member,
// regardless of stride), which cascaded into every `spirv.AccessChain`
// into it failing to legalize no matter which index pattern it used --
// even this one, a single member-selector index reaching the matrix in
// its own entirety, which getPhysicalMatrixMemberType's own transposed
// substitution can represent exactly. This case's `MatrixStride` (16)
// exactly matches one physical row's own natural size (4 columns of
// `f32`), so no padding is needed either.

// CHECK-LABEL: llvm.func @store_load
// CHECK-SAME: (%[[M:.*]]: !llvm.array<4 x vector<4xf32>>) -> !llvm.array<4 x vector<4xf32>>
// CHECK: %[[PTR:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK-SAME: -> !llvm.ptr<12>
// CHECK: llvm.store %{{.*}}, %[[PTR]] : !llvm.array<4 x array<4 x f32>>, !llvm.ptr<12>
// CHECK: %[[LOADED:.*]] = llvm.load %[[PTR]] : !llvm.ptr<12> -> !llvm.array<4 x array<4 x f32>>
// CHECK: llvm.return %{{.*}} : !llvm.array<4 x vector<4xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 0) : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, Uniform>
  spirv.func @store_load(%m : !spirv.matrix<4 x vector<4xf32>>) -> !spirv.matrix<4 x vector<4xf32>> "None" {
    %addr = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, Uniform>
    %c0 = spirv.Constant 0 : si32
    %elem = spirv.AccessChain %addr[%c0] : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, Uniform>, si32 -> !spirv.ptr<!spirv.matrix<4 x vector<4xf32>>, Uniform>
    spirv.Store "Uniform" %elem, %m : !spirv.matrix<4 x vector<4xf32>>
    %v = spirv.Load "Uniform" %elem : !spirv.matrix<4 x vector<4xf32>>
    spirv.ReturnValue %v : !spirv.matrix<4 x vector<4xf32>>
  }
  spirv.EntryPoint "GLCompute" @store_load
  spirv.ExecutionMode @store_load "LocalSize", 1, 1, 1
}
