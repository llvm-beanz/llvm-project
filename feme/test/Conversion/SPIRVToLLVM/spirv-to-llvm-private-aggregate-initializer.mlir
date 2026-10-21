// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L452(d): a `Private`-storage `spirv.GlobalVariable` whose
// `initial_value` is a `spirv.struct`/`spirv.array` composite (recursively,
// of any nesting) -- e.g. `dEQP-VK.spirv_assembly.instruction.*.
// variable_init.private.struct*`'s own mixed array+vector+scalar
// aggregate. Upstream MLIR's own `GlobalVariablePattern` explicitly
// restricts `initial_value` lowering to a scalar or vector pointee type,
// leaving a composite one unconverted, which then fails dialect-conversion
// legalization outright (`spirv.GlobalVariable` has no other pattern that
// can touch it).
//
// (The vector member is declared first, at offset 0, so its own natural
// 16-byte alignment already matches its declared offset -- putting it
// after the array instead would trigger `layOutStructIfOffsetsMatch`'s own
// unrelated "tight vector" marker-struct substitution, not exercised by
// this test.)
//
// CHECK-LABEL: llvm.mlir.global private @priv
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : !llvm.struct<packed (vector<4xf32>, array<2 x f32>, f32)>
// CHECK: %[[VEC:.*]] = llvm.mlir.constant(dense<3.000000e+00> : vector<4xf32>) : vector<4xf32>
// CHECK: %[[S1:.*]] = llvm.insertvalue %[[VEC]], %[[POISON]][0]
// CHECK: %[[ARR:.*]] = llvm.mlir.poison : !llvm.array<2 x f32>
// CHECK: %[[C0:.*]] = llvm.mlir.constant(1.000000e+00 : f32) : f32
// CHECK: %[[ARR1:.*]] = llvm.insertvalue %[[C0]], %[[ARR]][0]
// CHECK: %[[C1:.*]] = llvm.mlir.constant(2.000000e+00 : f32) : f32
// CHECK: %[[ARR2:.*]] = llvm.insertvalue %[[C1]], %[[ARR1]][1]
// CHECK: %[[S2:.*]] = llvm.insertvalue %[[ARR2]], %[[S1]][1]
// CHECK: %[[SC:.*]] = llvm.mlir.constant(4.000000e+00 : f32) : f32
// CHECK: %[[S3:.*]] = llvm.insertvalue %[[SC]], %[[S2]][2]
// CHECK: llvm.return %[[S3]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @priv initial_value([dense<3.0> : vector<4xf32>, [1.0 : f32, 2.0 : f32], 4.0 : f32]) : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], !spirv.array<2 x f32> [16], f32 [24])>, Private>
  spirv.func @read_priv() -> f32 "None" {
    %0 = spirv.mlir.addressof @priv : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], !spirv.array<2 x f32> [16], f32 [24])>, Private>
    %c2 = spirv.Constant 2 : i32
    %ac = spirv.AccessChain %0[%c2] : !spirv.ptr<!spirv.struct<(vector<4xf32> [0], !spirv.array<2 x f32> [16], f32 [24])>, Private>, i32 -> !spirv.ptr<f32, Private>
    %v = spirv.Load "Private" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

// -----

// Roadmap ENV5(b): `dEQP-VK.spirv_assembly.instruction.compute.
// variable_init.private.matrix`'s own `float2x4` global. A `spirv.matrix`
// `initial_value` is a different shape from the struct/array case above:
// the deserializer flattens it into a single `DenseElementsAttr` covering
// the whole matrix, not a nested `ArrayAttr` of per-column constants --
// `buildAggregateGlobalInitializerValue` splits that flat attribute one
// column's worth of elements at a time before recursing.

// CHECK-LABEL: llvm.mlir.global private @priv_mat
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : !llvm.array<2 x vector<4xf32>>
// CHECK: %[[COL0:.*]] = llvm.mlir.constant(dense<1.000000e+00> : vector<4xf32>) : vector<4xf32>
// CHECK: %[[S1:.*]] = llvm.insertvalue %[[COL0]], %[[POISON]][0]
// CHECK: %[[COL1:.*]] = llvm.mlir.constant(dense<1.000000e+00> : vector<4xf32>) : vector<4xf32>
// CHECK: %[[S2:.*]] = llvm.insertvalue %[[COL1]], %[[S1]][1]
// CHECK: llvm.return %[[S2]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @priv_mat initial_value(dense<1.0> : !spirv.matrix<2 x vector<4xf32>>) : !spirv.ptr<!spirv.matrix<2 x vector<4xf32>>, Private>
  spirv.func @read_priv_mat() -> f32 "None" {
    %0 = spirv.mlir.addressof @priv_mat : !spirv.ptr<!spirv.matrix<2 x vector<4xf32>>, Private>
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1] : !spirv.ptr<!spirv.matrix<2 x vector<4xf32>>, Private>, i32 -> !spirv.ptr<vector<4xf32>, Private>
    %c0 = spirv.Constant 0 : i32
    %ac2 = spirv.AccessChain %ac[%c0] : !spirv.ptr<vector<4xf32>, Private>, i32 -> !spirv.ptr<f32, Private>
    %v = spirv.Load "Private" %ac2 : f32
    spirv.ReturnValue %v : f32
  }
}
