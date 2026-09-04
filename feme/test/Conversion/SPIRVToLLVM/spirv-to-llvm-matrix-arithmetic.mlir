// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.MatrixTimesVector`/`spirv.VectorTimesMatrix`/
// `spirv.MatrixTimesMatrix`/`spirv.MatrixTimesScalar`/`spirv.Transpose`
// (roadmap H10f) all convert. MLIR has no pattern for any of this whole
// op family, discovered entirely unimplemented by a real Vulkan-CTS run
// (dEQP-VK.wsi.xcb.swapchain.render.basic et al.) failing to legalize an
// ordinary `mat4 * vec4` vertex transform. Every op lowers directly
// against a matrix's own `!llvm.array` of column vectors representation
// (see the `spirv.MatrixType` conversion in
// populateSPIRVToLLVMTargetTypeConversions and
// spirv-to-llvm-matrix-composite.mlir's own `CompositeExtract`/
// `CompositeInsert` precedent) using only `llvm.extractvalue`/
// `llvm.insertvalue` (whole-column access), `llvm.extractelement`/
// `llvm.insertelement` (within-column access), and a broadcast idiom
// (insert into lane 0 of a poison vector, then a zero-mask
// `llvm.shufflevector`) for scaling a column by a single scalar.

// `spirv.MatrixTimesVector`: `Matrix * Vector`, a per-column weighted sum
// (`result = sum_j vector[j] * column[j]`) -- no per-row reduction needed,
// since each input lane weights a whole output-shaped column directly.

// CHECK-LABEL: llvm.func @matrix_times_vector
// CHECK: %[[L0:.*]] = llvm.extractelement %arg1[%{{.*}} : i32] : vector<3xf32>
// CHECK: %[[POISON0:.*]] = llvm.mlir.poison : vector<2xf32>
// CHECK: %[[SEED0:.*]] = llvm.insertelement %[[L0]], %[[POISON0]][%{{.*}} : i32]
// CHECK: %[[W0:.*]] = llvm.shufflevector %[[SEED0]], %[[POISON0]] [0, 0] : vector<2xf32>
// CHECK: %[[C0:.*]] = llvm.extractvalue %arg0[0] : !llvm.array<3 x vector<2xf32>>
// CHECK: %[[T0:.*]] = llvm.fmul %[[W0]], %[[C0]] : vector<2xf32>
// CHECK: %[[L1:.*]] = llvm.extractelement %arg1[%{{.*}} : i32] : vector<3xf32>
// CHECK: %[[C1:.*]] = llvm.extractvalue %arg0[1] : !llvm.array<3 x vector<2xf32>>
// CHECK: %[[T1:.*]] = llvm.fmul %{{.*}}, %[[C1]] : vector<2xf32>
// CHECK: %[[ACC1:.*]] = llvm.fadd %[[T0]], %[[T1]] : vector<2xf32>
// CHECK: %[[L2:.*]] = llvm.extractelement %arg1[%{{.*}} : i32] : vector<3xf32>
// CHECK: %[[C2:.*]] = llvm.extractvalue %arg0[2] : !llvm.array<3 x vector<2xf32>>
// CHECK: %[[T2:.*]] = llvm.fmul %{{.*}}, %[[C2]] : vector<2xf32>
// CHECK: %[[ACC2:.*]] = llvm.fadd %[[ACC1]], %[[T2]] : vector<2xf32>
// CHECK: llvm.return %[[ACC2]] : vector<2xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Matrix], []> {
  spirv.func @matrix_times_vector(%m : !spirv.matrix<3 x vector<2xf32>>, %v : vector<3xf32>) -> vector<2xf32> "None" {
    %0 = spirv.MatrixTimesVector %m, %v : !spirv.matrix<3 x vector<2xf32>>, vector<3xf32> -> vector<2xf32>
    spirv.ReturnValue %0 : vector<2xf32>
  }
}

// -----

// `spirv.VectorTimesMatrix`: `Vector * Matrix`, one dot product (a full
// horizontal reduction) per output lane -- the matrix is on the right, so
// each output lane draws from a whole column rather than each input lane
// weighting a whole column.

// CHECK-LABEL: llvm.func @vector_times_matrix
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : vector<3xf32>
// CHECK: %[[C0:.*]] = llvm.extractvalue %arg1[0] : !llvm.array<3 x vector<2xf32>>
// CHECK: %[[P0:.*]] = llvm.fmul %arg0, %[[C0]] : vector<2xf32>
// CHECK: %[[E00:.*]] = llvm.extractelement %[[P0]][%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[E01:.*]] = llvm.extractelement %[[P0]][%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[DOT0:.*]] = llvm.fadd %[[E00]], %[[E01]] : f32
// CHECK: %[[V0:.*]] = llvm.insertelement %[[DOT0]], %[[POISON]][%{{.*}} : i32]
// CHECK: %[[C1:.*]] = llvm.extractvalue %arg1[1] : !llvm.array<3 x vector<2xf32>>
// CHECK: %[[V1:.*]] = llvm.insertelement %{{.*}}, %[[V0]][%{{.*}} : i32]
// CHECK: %[[C2:.*]] = llvm.extractvalue %arg1[2] : !llvm.array<3 x vector<2xf32>>
// CHECK: %[[V2:.*]] = llvm.insertelement %{{.*}}, %[[V1]][%{{.*}} : i32]
// CHECK: llvm.return %[[V2]] : vector<3xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Matrix], []> {
  spirv.func @vector_times_matrix(%v : vector<2xf32>, %m : !spirv.matrix<3 x vector<2xf32>>) -> vector<3xf32> "None" {
    %0 = spirv.VectorTimesMatrix %v, %m : vector<2xf32>, !spirv.matrix<3 x vector<2xf32>> -> vector<3xf32>
    spirv.ReturnValue %0 : vector<3xf32>
  }
}

// -----

// `spirv.MatrixTimesMatrix`: `LeftMatrix * RightMatrix`, column `j` of the
// result is `LeftMatrix * RightMatrix.column[j]` -- exactly the
// `MatrixTimesVector` product above, applied once per column of
// RightMatrix.

// CHECK-LABEL: llvm.func @matrix_times_matrix
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[RCOL0:.*]] = llvm.extractvalue %arg1[0] : !llvm.array<2 x vector<3xf32>>
// CHECK: %[[LCOL0:.*]] = llvm.extractvalue %arg0[0] : !llvm.array<3 x vector<2xf32>>
// CHECK: %[[RES0:.*]] = llvm.insertvalue %{{.*}}, %[[POISON]][0] : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[RCOL1:.*]] = llvm.extractvalue %arg1[1] : !llvm.array<2 x vector<3xf32>>
// CHECK: %[[RES1:.*]] = llvm.insertvalue %{{.*}}, %[[RES0]][1] : !llvm.array<2 x vector<2xf32>>
// CHECK: llvm.return %[[RES1]] : !llvm.array<2 x vector<2xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Matrix], []> {
  spirv.func @matrix_times_matrix(%l : !spirv.matrix<3 x vector<2xf32>>, %r : !spirv.matrix<2 x vector<3xf32>>) -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %0 = spirv.MatrixTimesMatrix %l, %r : !spirv.matrix<3 x vector<2xf32>>, !spirv.matrix<2 x vector<3xf32>> -> !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %0 : !spirv.matrix<2 x vector<2xf32>>
  }
}

// -----

// `spirv.MatrixTimesScalar`: scales every column by the same broadcast
// scalar.

// CHECK-LABEL: llvm.func @matrix_times_scalar
// CHECK: %[[POISON0:.*]] = llvm.mlir.poison : vector<2xf32>
// CHECK: %[[SEED:.*]] = llvm.insertelement %arg1, %[[POISON0]][%{{.*}} : i32]
// CHECK: %[[SCALAR:.*]] = llvm.shufflevector %[[SEED]], %[[POISON0]] [0, 0] : vector<2xf32>
// CHECK: %[[POISON1:.*]] = llvm.mlir.poison : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[C0:.*]] = llvm.extractvalue %arg0[0] : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[S0:.*]] = llvm.fmul %[[C0]], %[[SCALAR]] : vector<2xf32>
// CHECK: %[[RES0:.*]] = llvm.insertvalue %[[S0]], %[[POISON1]][0]
// CHECK: %[[C1:.*]] = llvm.extractvalue %arg0[1] : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[S1:.*]] = llvm.fmul %[[C1]], %[[SCALAR]] : vector<2xf32>
// CHECK: %[[RES1:.*]] = llvm.insertvalue %[[S1]], %[[RES0]][1]
// CHECK: llvm.return %[[RES1]] : !llvm.array<2 x vector<2xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Matrix], []> {
  spirv.func @matrix_times_scalar(%m : !spirv.matrix<2 x vector<2xf32>>, %s : f32) -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %0 = spirv.MatrixTimesScalar %m, %s : !spirv.matrix<2 x vector<2xf32>>, f32
    spirv.ReturnValue %0 : !spirv.matrix<2 x vector<2xf32>>
  }
}

// -----

// `spirv.Transpose`: builds each row `r` of the input (element `r` of
// every column) into column `r` of the result, one
// `llvm.extractelement`/`llvm.insertelement` pair per (row, column) cell.

// CHECK-LABEL: llvm.func @transpose
// CHECK: %[[RESULT:.*]] = llvm.mlir.poison : !llvm.array<3 x vector<2xf32>>
// CHECK: %[[ROW0:.*]] = llvm.mlir.poison : vector<2xf32>
// CHECK: %[[C0:.*]] = llvm.extractvalue %arg0[0] : !llvm.array<2 x vector<3xf32>>
// CHECK: %[[E00:.*]] = llvm.extractelement %[[C0]][%{{.*}} : i32] : vector<3xf32>
// CHECK: %[[ROW0A:.*]] = llvm.insertelement %[[E00]], %[[ROW0]][%{{.*}} : i32]
// CHECK: %[[C1:.*]] = llvm.extractvalue %arg0[1] : !llvm.array<2 x vector<3xf32>>
// CHECK: %[[E10:.*]] = llvm.extractelement %[[C1]][%{{.*}} : i32] : vector<3xf32>
// CHECK: %[[ROW0B:.*]] = llvm.insertelement %[[E10]], %[[ROW0A]][%{{.*}} : i32]
// CHECK: %[[RESULT0:.*]] = llvm.insertvalue %[[ROW0B]], %[[RESULT]][0]
// CHECK: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Matrix], []> {
  spirv.func @transpose(%m : !spirv.matrix<2 x vector<3xf32>>) -> !spirv.matrix<3 x vector<2xf32>> "None" {
    %0 = spirv.Transpose %m : !spirv.matrix<2 x vector<3xf32>> -> !spirv.matrix<3 x vector<2xf32>>
    spirv.ReturnValue %0 : !spirv.matrix<3 x vector<2xf32>>
  }
}
