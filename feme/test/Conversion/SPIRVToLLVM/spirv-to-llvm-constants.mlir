// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a `spirv.Constant` of `spirv.array` type converts, unlike
// MLIR's own `ConstantScalarAndVectorPattern`, which only matches a scalar
// or vector `spirv.Constant` and leaves an array one illegal -- exactly the
// shape a `const static` HLSL array (e.g. a palette of `float3`s) compiles
// down to.

// An array of vectors spells its value as an `ArrayAttr` of one
// `DenseElementsAttr` per vector element; this flattens to the single flat
// `DenseElementsAttr` `llvm.mlir.constant` accepts for the whole array.

// CHECK-LABEL: llvm.func @palette
// CHECK: llvm.mlir.constant(dense<[0.000000e+00, 0.000000e+00, 0.000000e+00, 1.000000e+00, 5.000000e-01, 2.500000e-01]> : tensor<6xf32>) : !llvm.array<2 x vector<3xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @palette() -> !spirv.array<2 x vector<3xf32>> "None" {
    %0 = spirv.Constant [dense<0.0> : vector<3xf32>, dense<[1.0, 0.5, 0.25]> : vector<3xf32>] : !spirv.array<2 x vector<3xf32>>
    spirv.ReturnValue %0 : !spirv.array<2 x vector<3xf32>>
  }
}

// -----

// An array of scalars already spells its value as a single flat
// `DenseElementsAttr` (no per-element `ArrayAttr` wrapping needed, unlike
// the vector-element case above), which flattens to itself unchanged.

// CHECK-LABEL: llvm.func @intarray
// CHECK: llvm.mlir.constant(dense<[1, 2, 3]> : tensor<3xi32>) : !llvm.array<3 x i32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @intarray() -> !spirv.array<3 x i32> "None" {
    %0 = spirv.Constant dense<[1, 2, 3]> : tensor<3xi32> : !spirv.array<3 x i32>
    spirv.ReturnValue %0 : !spirv.array<3 x i32>
  }
}

// -----

// A `spirv.Constant` of `spirv.matrix` type converts the same way as the
// array cases above: a matrix converts to the identical target shape, an
// `!llvm.array` of column vectors (see the `spirv.MatrixType` conversion in
// populateSPIRVToLLVMTargetTypeConversions), and its value already spells
// as one flat `DenseElementsAttr` (unlike the array-of-vectors case, no
// per-column `ArrayAttr` wrapping), so this pattern's own array/matrix type
// check is the only change needed -- the flattening and re-encoding logic
// is unchanged. This is the shape a `const static float2x2` HLSL matrix
// compiles down to.

// CHECK-LABEL: llvm.func @const_matrix
// CHECK: llvm.mlir.constant(dense<[1.000000e+00, 2.000000e+00, 3.000000e+00, 4.000000e+00]> : tensor<4xf32>) : !llvm.array<2 x vector<2xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @const_matrix() -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %0 = spirv.Constant dense<[[1.0, 2.0], [3.0, 4.0]]> : !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %0 : !spirv.matrix<2 x vector<2xf32>>
  }
}
