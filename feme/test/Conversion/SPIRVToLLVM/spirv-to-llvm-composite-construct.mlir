// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.CompositeConstruct` building a vector, struct, or
// matrix converts, which MLIR has no pattern for at all: a vector result
// lowers to an `llvm.mlir.poison` seed with one `llvm.insertelement` per
// lane; a struct result lowers similarly with one `llvm.insertvalue` per
// member; a matrix result (roadmap H10d) lowers similarly with one
// `llvm.insertvalue` per column.

// A splat (e.g. HLSL's `.xxx` swizzle) constructs every lane from the same
// scalar constituent.

// CHECK-LABEL: llvm.func @splat
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : vector<3xf32>
// CHECK: %[[I0:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[V0:.*]] = llvm.insertelement %arg0, %[[POISON]][%[[I0]] : i32]
// CHECK: %[[I1:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[V1:.*]] = llvm.insertelement %arg0, %[[V0]][%[[I1]] : i32]
// CHECK: %[[I2:.*]] = llvm.mlir.constant(2 : i32) : i32
// CHECK: %[[V2:.*]] = llvm.insertelement %arg0, %[[V1]][%[[I2]] : i32]
// CHECK: llvm.return %[[V2]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @splat(%x : f32) -> vector<3xf32> "None" {
    %0 = spirv.CompositeConstruct %x, %x, %x : (f32, f32, f32) -> vector<3xf32>
    spirv.ReturnValue %0 : vector<3xf32>
  }
}

// -----

// A vector constituent supplies a contiguous run of lanes, extracted one at
// a time; a scalar constituent supplies a single lane directly.

// CHECK-LABEL: llvm.func @mixed
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : vector<3xf32>
// CHECK: %[[E0:.*]] = llvm.extractelement %arg0[%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[V0:.*]] = llvm.insertelement %[[E0]], %[[POISON]][%{{.*}} : i32]
// CHECK: %[[E1:.*]] = llvm.extractelement %arg0[%{{.*}} : i32] : vector<2xf32>
// CHECK: %[[V1:.*]] = llvm.insertelement %[[E1]], %[[V0]][%{{.*}} : i32]
// CHECK: llvm.insertelement %arg1, %[[V1]][%{{.*}} : i32]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @mixed(%v : vector<2xf32>, %z : f32) -> vector<3xf32> "None" {
    %0 = spirv.CompositeConstruct %v, %z : (vector<2xf32>, f32) -> vector<3xf32>
    spirv.ReturnValue %0 : vector<3xf32>
  }
}

// -----

// Checks that `spirv.CompositeConstruct` building a struct value (e.g. a
// whole HLSL struct assembled before storing it in one shot) converts,
// which MLIR also has no pattern for at all: it lowers to an
// `llvm.mlir.poison` seed with one `llvm.insertvalue` per member, in
// order. Every member here lays out naturally (no roadmap L13 tight-vector
// substitution needed), so each constituent's type matches its member's
// converted type exactly and no `llvm.bitcast` is emitted.

// CHECK-LABEL: llvm.func @construct_struct
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : !llvm.struct<(i32, vector<4xf32>)>
// CHECK: %[[V0:.*]] = llvm.insertvalue %arg0, %[[POISON]][0]
// CHECK: %[[V1:.*]] = llvm.insertvalue %arg1, %[[V0]][1]
// CHECK: llvm.return %[[V1]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @construct_struct(%x : si32, %v : vector<4xf32>) -> !spirv.struct<Naturally, (si32 [0], vector<4xf32> [16])> "None" {
    %0 = spirv.CompositeConstruct %x, %v : (si32, vector<4xf32>) -> !spirv.struct<Naturally, (si32 [0], vector<4xf32> [16])>
    spirv.ReturnValue %0 : !spirv.struct<Naturally, (si32 [0], vector<4xf32> [16])>
  }
}

// -----

// Checks that `spirv.CompositeConstruct` building a struct value with a
// roadmap L13 tight-vector-substituted member (a `vector<3xsi32>` member
// whose declared offset -- see spirv-to-llvm-nested-identified-struct.mlir
// -- only lays out as a tightly-packed `!llvm.array<3 x i32>`, not a real
// LLVM vector) reassembles the real-vector constituent into the
// substituted array type lane-by-lane before inserting it (`llvm.bitcast`
// cannot do this directly, since its own verifier requires a
// non-aggregate result), since `llvm.insertvalue` requires the inserted
// value's type to match the struct's declared field type exactly.

// CHECK-LABEL: llvm.func @construct_tight_vector_struct
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : !llvm.struct<(array<3 x i32>, i32)>
// CHECK: %[[ARR:.*]] = llvm.mlir.poison : !llvm.array<3 x i32>
// CHECK: %[[E0:.*]] = llvm.extractelement %arg0[%{{.*}} : i32] : vector<3xi32>
// CHECK: %[[A0:.*]] = llvm.insertvalue %[[E0]], %[[ARR]][0]
// CHECK: %[[E1:.*]] = llvm.extractelement %arg0[%{{.*}} : i32] : vector<3xi32>
// CHECK: %[[A1:.*]] = llvm.insertvalue %[[E1]], %[[A0]][1]
// CHECK: %[[E2:.*]] = llvm.extractelement %arg0[%{{.*}} : i32] : vector<3xi32>
// CHECK: %[[A2:.*]] = llvm.insertvalue %[[E2]], %[[A1]][2]
// CHECK: %[[V0:.*]] = llvm.insertvalue %[[A2]], %[[POISON]][0]
// CHECK: %[[V1:.*]] = llvm.insertvalue %arg1, %[[V0]][1]
// CHECK: llvm.return %[[V1]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @construct_tight_vector_struct(%legs : vector<3xsi32>, %tail : si32) -> !spirv.struct<Doggo, (vector<3xsi32> [0], si32 [12])> "None" {
    %0 = spirv.CompositeConstruct %legs, %tail : (vector<3xsi32>, si32) -> !spirv.struct<Doggo, (vector<3xsi32> [0], si32 [12])>
    spirv.ReturnValue %0 : !spirv.struct<Doggo, (vector<3xsi32> [0], si32 [12])>
  }
}

// -----

// Checks that `spirv.CompositeConstruct` building a matrix value (roadmap
// H10d, e.g. GLSL's `mat4(c0, c1, c2, c3)`, or the `mat2` case here) from
// one whole column-vector constituent per column converts: MLIR has no
// pattern at all for a matrix-result `CompositeConstruct`, and this ICD
// previously had none either, discovered by a real Vulkan-CTS run
// (dEQP-VK.wsi.xcb.swapchain.render.*) failing `vkCreateGraphicsPipelines`
// with `"failed to legalize operation 'spirv.CompositeConstruct' that was
// explicitly marked illegal"`. Lowers to an `llvm.mlir.poison` seed (the
// matrix's own `!llvm.array<N x column vector>` representation, see
// spirv-to-llvm-matrix-composite.mlir) with one `llvm.insertvalue` per
// column, each constituent inserted as-is.

// CHECK-LABEL: llvm.func @construct_matrix
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : !llvm.array<2 x vector<2xf32>>
// CHECK: %[[V0:.*]] = llvm.insertvalue %arg0, %[[POISON]][0]
// CHECK: %[[V1:.*]] = llvm.insertvalue %arg1, %[[V0]][1]
// CHECK: llvm.return %[[V1]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @construct_matrix(%c0 : vector<2xf32>, %c1 : vector<2xf32>) -> !spirv.matrix<2 x vector<2xf32>> "None" {
    %0 = spirv.CompositeConstruct %c0, %c1 : (vector<2xf32>, vector<2xf32>) -> !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %0 : !spirv.matrix<2 x vector<2xf32>>
  }
}
