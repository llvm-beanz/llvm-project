// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// A `ColMajor` matrix member -- the shape a plain GLSL
// `uniform UBO { mat4 m; };` compiles to -- converts to LLVM's own natural
// column-major representation, an array of column vectors, matching real
// backend-emitted IR (see
// `llvm/test/CodeGen/SPIRV/pointers/load-store-matrix-in-struct.ll`); its
// `MatrixStride` matches that representation's natural per-column stride
// (16 bytes, `sizeof(vector<4xf32>)`), so the whole struct converts.

// CHECK-LABEL: llvm.func @read_column
// CHECK: %[[HANDLE:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(array<4 x vector<4xf32>>)>, 2, 0>
// CHECK: %[[MEMBER:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE]], %{{.*}})
// CHECK: %[[COL:.*]] = llvm.getelementptr inbounds %[[MEMBER]][0, %{{.*}}]
// CHECK: llvm.load %[[COL]] : !llvm.ptr<12> -> vector<4xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 5) : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, ColMajor, MatrixStride=16]), Block>, Uniform>
  spirv.func @read_column(%idx : i32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, ColMajor, MatrixStride=16]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, ColMajor, MatrixStride=16]), Block>, Uniform>, i32, i32 -> !spirv.ptr<vector<4xf32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<4xf32>
    spirv.ReturnValue %v : vector<4xf32>
  }
}
