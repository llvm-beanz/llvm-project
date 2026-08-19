// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks `spirv.CompositeExtract`/`spirv.CompositeInsert` on a matrix.
// Found by a real Vulkan-CTS run
// (dEQP-VK.glsl.conversions.matrix_to_matrix.mat2_to_mat2x3_vertex): MLIR's
// own `CompositeExtractPattern`/`CompositeInsertPattern` assume any
// non-`VectorType` container converts to a pure `llvm.extractvalue`/
// `llvm.insertvalue`-shaped aggregate (struct/array nesting all the way
// down), which is wrong once a matrix's own array-of-column-vectors
// representation is reached: an index selecting a scalar *within* a column
// needs `llvm.extractelement`/`llvm.insertelement`, not another
// `llvm.extractvalue`/`llvm.insertvalue` level, since a vector is not an
// aggregate LLVM's own `extractvalue`/`insertvalue` can index into.

// CHECK-LABEL: llvm.func @extract_scalar
// CHECK: %[[COL:.*]] = llvm.extractvalue %arg0[0] : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[ROW:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[ELEM:.*]] = llvm.extractelement %[[COL]][%[[ROW]] : i32] : vector<2xf32>
// CHECK: llvm.return %[[ELEM]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @extract_scalar(%m : !spirv.matrix<2 x vector<2xf32>>) -> f32 "None" {
    %0 = spirv.CompositeExtract %m[0 : i32, 1 : i32] : !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %0 : f32
  }
}

// -----

// A single index extracts a whole column: an ordinary `llvm.extractvalue`,
// exactly as MLIR's own pattern would have produced.

// CHECK-LABEL: llvm.func @extract_column
// CHECK: %[[COL:.*]] = llvm.extractvalue %arg0[0] : !llvm.array<2 x vector<2xf32>>
// CHECK: llvm.return %[[COL]] : vector<2xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @extract_column(%m : !spirv.matrix<2 x vector<2xf32>>) -> vector<2xf32> "None" {
    %0 = spirv.CompositeExtract %m[0 : i32] : !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %0 : vector<2xf32>
  }
}

// -----

// Inserting a scalar reads the column back with `llvm.extractvalue`,
// updates it with `llvm.insertelement`, and writes it back with
// `llvm.insertvalue`.

// CHECK-LABEL: llvm.func @insert_scalar
// CHECK: %[[COL:.*]] = llvm.extractvalue %arg0[0] : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[ROW:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[UPDATED:.*]] = llvm.insertelement %arg1, %[[COL]][%[[ROW]] : i32] : vector<2xf32>
// CHECK: %[[RESULT:.*]] = llvm.insertvalue %[[UPDATED]], %arg0[0] : !llvm.array<2 x vector<2xf32>>
// CHECK: llvm.return %[[RESULT]] : !llvm.array<2 x vector<2xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @insert_scalar(%m : !spirv.matrix<2 x vector<2xf32>>, %v : f32) -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %0 = spirv.CompositeInsert %v, %m[0 : i32, 1 : i32] : f32 into !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %0 : !spirv.matrix<2 x vector<2xf32>>
  }
}

// -----

// Inserting a whole column is an ordinary `llvm.insertvalue`.

// CHECK-LABEL: llvm.func @insert_column
// CHECK: %[[RESULT:.*]] = llvm.insertvalue %arg1, %arg0[0] : !llvm.array<2 x vector<2xf32>>
// CHECK: llvm.return %[[RESULT]] : !llvm.array<2 x vector<2xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @insert_column(%m : !spirv.matrix<2 x vector<2xf32>>, %c : vector<2xf32>) -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %0 = spirv.CompositeInsert %c, %m[0 : i32] : vector<2xf32> into !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %0 : !spirv.matrix<2 x vector<2xf32>>
  }
}
