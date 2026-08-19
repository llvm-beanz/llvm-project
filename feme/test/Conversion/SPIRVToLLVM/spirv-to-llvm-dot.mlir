// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.Dot` converts to a per-lane `llvm.intr.fmuladd` chain,
// which MLIR has no pattern for at all, mirroring
// `feme::dxil::expandFDot`'s expansion of the analogous (post-raising)
// `llvm.dx.fdot` intrinsic on the DXIL side.

// CHECK-LABEL: llvm.func @dot3
// CHECK: %[[A0:.*]] = llvm.extractelement %arg0[%{{.*}} : i64] : vector<3xf32>
// CHECK: %[[B0:.*]] = llvm.extractelement %arg1[%{{.*}} : i64] : vector<3xf32>
// CHECK: %[[MUL:.*]] = llvm.fmul %[[A0]], %[[B0]] : f32
// CHECK: %[[A1:.*]] = llvm.extractelement %arg0[%{{.*}} : i64] : vector<3xf32>
// CHECK: %[[B1:.*]] = llvm.extractelement %arg1[%{{.*}} : i64] : vector<3xf32>
// CHECK: %[[FMA1:.*]] = llvm.intr.fmuladd(%[[A1]], %[[B1]], %[[MUL]])
// CHECK: %[[A2:.*]] = llvm.extractelement %arg0[%{{.*}} : i64] : vector<3xf32>
// CHECK: %[[B2:.*]] = llvm.extractelement %arg1[%{{.*}} : i64] : vector<3xf32>
// CHECK: %[[FMA2:.*]] = llvm.intr.fmuladd(%[[A2]], %[[B2]], %[[FMA1]])
// CHECK: llvm.return %[[FMA2]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @dot3(%a : vector<3xf32>, %b : vector<3xf32>) -> f32 "None" {
    %0 = spirv.Dot %a, %b : vector<3xf32> -> f32
    spirv.ReturnValue %0 : f32
  }
}
