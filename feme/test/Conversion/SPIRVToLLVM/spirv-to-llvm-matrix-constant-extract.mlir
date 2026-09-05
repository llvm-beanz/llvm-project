// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.CompositeExtract` on a `spirv.matrix`-typed
// `spirv.Constant` converts to the correct per-column sub-constant, both
// for a square (2x2) and a non-square (4x3) matrix (roadmap L38).
//
// Unlike feme's own `spirv-to-llvm-matrix-composite.mlir` test (which
// exercises `MatrixCompositeExtractPattern` on a non-constant matrix, e.g.
// a function argument), a matrix *constant*'s `spirv.CompositeExtract` is
// never actually legalized via that pattern at all: MLIR's dialect
// conversion driver first tries `spirv::CompositeExtractOp::fold`
// (`OperationLegalizer::legalizeWithFold`), which succeeds directly against
// the constant's own `ElementsAttr` value -- bypassing feme's own pattern
// entirely. `extractCompositeElement`'s upstream MLIR implementation
// (`mlir/lib/Dialect/SPIRV/IR/SPIRVCanonicalization.cpp`) previously
// treated any single index into an `ElementsAttr` composite as one flat
// scalar offset, correct only for a plain vector constant, not a matrix
// constant's whole-column selection -- discovered via
// dEQP-VK.glsl.matrix.add.const.highp_mat2_float_fragment (square, wrong
// scalar picked) and .mediump_mat4x3_float_vertex (non-square, wrong
// element count entirely once a naive shape-based fix was tried).

// CHECK-LABEL: llvm.func @matrix_const_column_square
// CHECK: llvm.mlir.constant(dense<[-2.000000e-01, 0.000000e+00]> : vector<2xf32>) : vector<2xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @matrix_const_column_square() -> vector<2xf32> "None" {
    %0 = spirv.Constant dense<[[-0.1, 1.0], [-0.2, 0.0]]> : !spirv.matrix<2 x vector<2xf32>>
    %1 = spirv.CompositeExtract %0[1 : i32] : !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %1 : vector<2xf32>
  }
}

// -----

// A non-square matrix (more columns than rows) is the case that exposes a
// column offset computed against the wrong dimension: the flat storage is
// grouped by `numRows`-sized chunks (one chunk per column), which differs
// from `numColumns` here (3 vs. 4).

// CHECK-LABEL: llvm.func @matrix_const_column_non_square
// CHECK: llvm.mlir.constant(dense<[2.000000e-01, 0.899999976, -1.000000e-01]> : vector<3xf32>) : vector<3xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @matrix_const_column_non_square() -> vector<3xf32> "None" {
    %0 = spirv.Constant dense<[[-0.9, 0.0, 0.6, 0.2], [0.9, -0.1, -0.3, -0.7], [-0.1, 0.1, 1.0, 0.0]]> : !spirv.matrix<4 x vector<3xf32>>
    %1 = spirv.CompositeExtract %0[1 : i32] : !spirv.matrix<4 x vector<3xf32>>
    spirv.ReturnValue %1 : vector<3xf32>
  }
}

// -----

// A two-index extract on a matrix constant selects a single scalar: the
// first index picks the column, the second picks a row within it.

// CHECK-LABEL: llvm.func @matrix_const_element
// CHECK: llvm.mlir.constant(-2.000000e-01 : f32) : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @matrix_const_element() -> f32 "None" {
    %0 = spirv.Constant dense<[[-0.1, 1.0], [-0.2, 0.0]]> : !spirv.matrix<2 x vector<2xf32>>
    %1 = spirv.CompositeExtract %0[1 : i32, 0 : i32] : !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %1 : f32
  }
}
