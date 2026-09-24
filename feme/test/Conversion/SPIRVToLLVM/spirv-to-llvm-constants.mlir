// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a `spirv.Constant` of `spirv.array` type converts, unlike
// MLIR's own `ConstantScalarAndVectorPattern`, which only matches a scalar
// or vector `spirv.Constant` and leaves an array one illegal -- exactly the
// shape a `const static` HLSL array (e.g. a palette of `float3`s) compiles
// down to.

// An array of vectors spells its value as an `ArrayAttr` of one
// `DenseElementsAttr` per vector element; this flattens to a single
// `DenseElementsAttr` `llvm.mlir.constant` accepts for the whole array,
// shaped as a multi-dimensional `vector<...>` (not a flat `tensor<...>`,
// see `getFlatElementShape`/`hasVectorLeaf`'s own comments in
// SPIRVToLLVMPatterns.cpp) matching the array's own array-of-vector
// nesting, so upstream MLIR's own LLVM IR translation
// (`convertDenseElementsAttr`) reassembles the vector leaves correctly
// (roadmap L29).

// CHECK-LABEL: llvm.func @palette
// CHECK: llvm.mlir.constant(dense<{{\[}}[0.000000e+00, 0.000000e+00, 0.000000e+00], [1.000000e+00, 5.000000e-01, 2.500000e-01]]> : vector<2x3xf32>) : !llvm.array<2 x vector<3xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @palette() -> !spirv.array<2 x vector<3xf32>> "None" {
    %0 = spirv.Constant [dense<0.0> : vector<3xf32>, dense<[1.0, 0.5, 0.25]> : vector<3xf32>] : !spirv.array<2 x vector<3xf32>>
    spirv.ReturnValue %0 : !spirv.array<2 x vector<3xf32>>
  }
}

// -----

// An array of scalars already spells its value as a single flat
// `DenseElementsAttr` (no per-element `ArrayAttr` wrapping needed, unlike
// the vector-element case above), which flattens to itself unchanged: a
// plain scalar leaf (no `vector<...>` anywhere in the array's nesting)
// keeps a flat `tensor<...>` shape, since upstream's own LLVM IR
// translation already reassembles that correctly (see `hasVectorLeaf`).

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
// is unchanged, including reshaping into a multi-dimensional `vector<...>`
// (roadmap L29, see the `@palette` case above) since a matrix's own
// per-column leaf is a vector. This is the shape a `const static float2x2`
// HLSL matrix compiles down to.

// CHECK-LABEL: llvm.func @const_matrix
// CHECK: llvm.mlir.constant(dense<{{\[}}[1.000000e+00, 2.000000e+00], [3.000000e+00, 4.000000e+00]]> : vector<2x2xf32>) : !llvm.array<2 x vector<2xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @const_matrix() -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %0 = spirv.Constant dense<[[1.0, 2.0], [3.0, 4.0]]> : !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %0 : !spirv.matrix<2 x vector<2xf32>>
  }
}

// -----

// (Roadmap L116(d)) A `spirv.Constant` of an array-of-struct type -- e.g.
// the shape an HLSL struct array compiles down to -- has no legal
// `ElementsAttr` encoding at all (LLVM's own element-count computation
// treats a `!llvm.struct` as a single opaque leaf, see
// `LLVM::ConstantOp::verify`), so `ArrayConstantPattern` above always
// rejects it. `StructConstantPattern` (SPIRVToLLVMPatterns.cpp) instead
// decomposes it one aggregate level at a time -- an array-of-struct into
// one new, simpler `spirv.Constant` per element feeding a
// `spirv.CompositeConstruct`, cascading recursively for any further struct
// nesting -- into a tree of scalar/array constants and `insertvalue`s,
// with no `spirv.Constant`/`spirv.CompositeConstruct` left unconverted.

// CHECK-LABEL: llvm.func @array_of_struct_of_array
// CHECK-DAG: llvm.mlir.constant(0.000000e+00 : f32) : f32
// CHECK-DAG: llvm.mlir.constant(dense<[1.000000e+00, 2.000000e+00]> : tensor<2xf32>) : !llvm.array<2 x f32>
// CHECK-DAG: llvm.mlir.constant(3.000000e+00 : f32) : f32
// CHECK-DAG: llvm.mlir.constant(dense<[4.000000e+00, 5.000000e+00]> : tensor<2xf32>) : !llvm.array<2 x f32>
// CHECK-COUNT-4: llvm.insertvalue
// CHECK-NOT: spirv.Constant
// CHECK-NOT: spirv.CompositeConstruct
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @array_of_struct_of_array() -> !spirv.array<2 x !spirv.struct<(f32, !spirv.array<2 x f32>)>> "None" {
    %0 = spirv.Constant [
      [0.0 : f32, dense<[1.0, 2.0]> : tensor<2xf32>],
      [3.0 : f32, dense<[4.0, 5.0]> : tensor<2xf32>]
    ] : !spirv.array<2 x !spirv.struct<(f32, !spirv.array<2 x f32>)>>
    spirv.ReturnValue %0 : !spirv.array<2 x !spirv.struct<(f32, !spirv.array<2 x f32>)>>
  }
}
