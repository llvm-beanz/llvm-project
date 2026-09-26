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

// -----

// `spirv.GL.Determinant` (roadmap L116(c)): Laplace expansion along the
// first row. For a 2x2 matrix this collapses to the ordinary `ad - bc`
// formula (element(row,col): a=[0][0], b=[0][1], c=[1][0], d=[1][1]).

// CHECK-LABEL: llvm.func @determinant_2x2
// CHECK: %[[C0:.*]] = llvm.extractvalue %arg0[0] : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[A:.*]] = llvm.extractelement %[[C0]][%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[C1:.*]] = llvm.extractvalue %arg0[1] : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[B:.*]] = llvm.extractelement %[[C1]][%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[C0B:.*]] = llvm.extractvalue %arg0[0] : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[C:.*]] = llvm.extractelement %[[C0B]][%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[C1B:.*]] = llvm.extractvalue %arg0[1] : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[D:.*]] = llvm.extractelement %[[C1B]][%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[AD:.*]] = llvm.fmul %[[A]], %[[D]] : f32
// CHECK: %[[BC:.*]] = llvm.fmul %[[B]], %[[C]] : f32
// CHECK: %[[DET:.*]] = llvm.fsub %[[AD]], %[[BC]] : f32
// CHECK: llvm.return %[[DET]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Matrix], []> {
  spirv.func @determinant_2x2(%m : !spirv.matrix<2 x vector<2xf32>>) -> f32 "None" {
    %0 = spirv.GL.Determinant %m : !spirv.matrix<2 x vector<2xf32>> -> f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// A 3x3 determinant recurses one level deeper: the first-row expansion's
// three 2x2 minors, each built and combined the same way as the 2x2 case
// above, alternating +/- across the three terms.

// CHECK-LABEL: llvm.func @determinant_3x3
// CHECK-COUNT-9: llvm.extractelement
// CHECK: %[[MINOR0:.*]] = llvm.fsub {{.*}} : f32
// CHECK: %[[TERM0:.*]] = llvm.fmul {{.*}}, %[[MINOR0]] : f32
// CHECK: %[[MINOR1:.*]] = llvm.fsub {{.*}} : f32
// CHECK: %[[TERM1:.*]] = llvm.fmul {{.*}}, %[[MINOR1]] : f32
// CHECK: %[[ACC1:.*]] = llvm.fsub %[[TERM0]], %[[TERM1]] : f32
// CHECK: %[[MINOR2:.*]] = llvm.fsub {{.*}} : f32
// CHECK: %[[TERM2:.*]] = llvm.fmul {{.*}}, %[[MINOR2]] : f32
// CHECK: %[[ACC2:.*]] = llvm.fadd %[[ACC1]], %[[TERM2]] : f32
// CHECK: llvm.return %[[ACC2]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Matrix], []> {
  spirv.func @determinant_3x3(%m : !spirv.matrix<3 x vector<3xf32>>) -> f32 "None" {
    %0 = spirv.GL.Determinant %m : !spirv.matrix<3 x vector<3xf32>> -> f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// `spirv.GL.MatrixInverse` (roadmap L201): adjugate-over-determinant, i.e.
// `Inverse[i][j] = Cofactor(j, i) / Determinant`. For a 2x2 matrix
// (element(row,col): a=[0][0], b=[0][1], c=[1][0], d=[1][1]) this is the
// textbook `1/det * [[d, -b], [-c, a]]` -- each cofactor is itself a
// (trivial, 1x1) determinant, negated for the two off-diagonal (odd
// row+col) cofactors, then every element divided by the shared
// determinant before the result matrix is rebuilt column-by-column.

// CHECK-LABEL: llvm.func @matrix_inverse_2x2
// CHECK-COUNT-4: llvm.extractelement
// CHECK: %[[AD:.*]] = llvm.fmul {{.*}} : f32
// CHECK: %[[BC:.*]] = llvm.fmul {{.*}} : f32
// CHECK: %[[DET:.*]] = llvm.fsub %[[AD]], %[[BC]] : f32
// CHECK: %[[INV00:.*]] = llvm.fdiv {{.*}}, %[[DET]] : f32
// CHECK: %[[NEGC:.*]] = llvm.fneg {{.*}} : f32
// CHECK: %[[INV10:.*]] = llvm.fdiv %[[NEGC]], %[[DET]] : f32
// CHECK: %[[NEGB:.*]] = llvm.fneg {{.*}} : f32
// CHECK: %[[INV01:.*]] = llvm.fdiv %[[NEGB]], %[[DET]] : f32
// CHECK: %[[INV11:.*]] = llvm.fdiv {{.*}}, %[[DET]] : f32
// CHECK: %[[RESULT:.*]] = llvm.mlir.poison : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[COL0POISON:.*]] = llvm.mlir.poison : vector<2xf32>
// CHECK: %[[COL0A:.*]] = llvm.insertelement %[[INV00]], %[[COL0POISON]]
// CHECK: %[[COL0B:.*]] = llvm.insertelement %[[INV10]], %[[COL0A]]
// CHECK: %[[RES0:.*]] = llvm.insertvalue %[[COL0B]], %[[RESULT]][0]
// CHECK: %[[COL1POISON:.*]] = llvm.mlir.poison : vector<2xf32>
// CHECK: %[[COL1A:.*]] = llvm.insertelement %[[INV01]], %[[COL1POISON]]
// CHECK: %[[COL1B:.*]] = llvm.insertelement %[[INV11]], %[[COL1A]]
// CHECK: %[[RES1:.*]] = llvm.insertvalue %[[COL1B]], %[[RES0]][1]
// CHECK: llvm.return %[[RES1]] : !llvm.array<2 x vector<2xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Matrix], []> {
  spirv.func @matrix_inverse_2x2(%m : !spirv.matrix<2 x vector<2xf32>>) -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %0 = spirv.GL.MatrixInverse %m : !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %0 : !spirv.matrix<2 x vector<2xf32>>
  }
}

// -----

// `spirv.OuterProduct` (roadmap L201): column `Col` of the result is
// `Vector1` scaled by the broadcast of lane `Col` of `Vector2` -- the
// same scalar-broadcast-and-multiply idiom `MatrixTimesScalarPattern`
// uses, just once per column with a different scalar each time instead
// of one shared scalar for the whole matrix.

// CHECK-LABEL: llvm.func @outer_product_2x2
// CHECK: %[[RESULT:.*]] = llvm.mlir.poison : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[LANE0:.*]] = llvm.extractelement %arg1[%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[SEED0:.*]] = llvm.mlir.poison : vector<2xf32>
// CHECK: %[[BROADCAST0A:.*]] = llvm.insertelement %[[LANE0]], %[[SEED0]]
// CHECK: %[[BROADCAST0:.*]] = llvm.shufflevector %[[BROADCAST0A]], %[[SEED0]] [0, 0]
// CHECK: %[[COL0:.*]] = llvm.fmul %arg0, %[[BROADCAST0]] : vector<2xf32>
// CHECK: %[[RES0:.*]] = llvm.insertvalue %[[COL0]], %[[RESULT]][0]
// CHECK: %[[LANE1:.*]] = llvm.extractelement %arg1[%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[SEED1:.*]] = llvm.mlir.poison : vector<2xf32>
// CHECK: %[[BROADCAST1A:.*]] = llvm.insertelement %[[LANE1]], %[[SEED1]]
// CHECK: %[[BROADCAST1:.*]] = llvm.shufflevector %[[BROADCAST1A]], %[[SEED1]] [0, 0]
// CHECK: %[[COL1:.*]] = llvm.fmul %arg0, %[[BROADCAST1]] : vector<2xf32>
// CHECK: %[[RES1:.*]] = llvm.insertvalue %[[COL1]], %[[RES0]][1]
// CHECK: llvm.return %[[RES1]] : !llvm.array<2 x vector<2xf32>>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Matrix], []> {
  spirv.func @outer_product_2x2(%v1 : vector<2xf32>, %v2 : vector<2xf32>) -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %0 = spirv.OuterProduct %v1, %v2 : vector<2xf32>, vector<2xf32> -> !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %0 : !spirv.matrix<2 x vector<2xf32>>
  }
}
