// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.CompositeConstruct` building a vector, struct, matrix,
// or array converts, which MLIR has no pattern for at all: a vector result
// lowers to an `llvm.mlir.poison` seed with one `llvm.insertelement` per
// lane; a struct result lowers similarly with one `llvm.insertvalue` per
// member; a matrix result (roadmap H10d) lowers similarly with one
// `llvm.insertvalue` per column; an array result (roadmap L22) lowers the
// same way as the matrix case, with one `llvm.insertvalue` per element.

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
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : !llvm.struct<packed (i32, array<12 x i8>, vector<4xf32>)>
// CHECK: %[[V0:.*]] = llvm.insertvalue %arg0, %[[POISON]][0]
// CHECK: %[[V1:.*]] = llvm.insertvalue %arg1, %[[V0]][2]
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
//
// Roadmap H101j: the substituted array is now wrapped in a uniquely-named
// `!llvm.struct<"feme.tight_vector", ...>` marker struct (see
// spirv-to-llvm-nested-identified-struct.mlir's own comment for why), so
// the reassembled array must itself be re-wrapped in the marker (one
// extra `llvm.insertvalue` into a poisoned marker struct) before it is
// inserted into the outer struct's own field.

// CHECK-LABEL: llvm.func @construct_tight_vector_struct
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : !llvm.struct<packed (struct<"feme.tight_vector", (array<3 x i32>)>, i32)>
// CHECK: %[[ARR:.*]] = llvm.mlir.poison : !llvm.array<3 x i32>
// CHECK: %[[E0:.*]] = llvm.extractelement %arg0[%{{.*}} : i32] : vector<3xi32>
// CHECK: %[[A0:.*]] = llvm.insertvalue %[[E0]], %[[ARR]][0]
// CHECK: %[[E1:.*]] = llvm.extractelement %arg0[%{{.*}} : i32] : vector<3xi32>
// CHECK: %[[A1:.*]] = llvm.insertvalue %[[E1]], %[[A0]][1]
// CHECK: %[[E2:.*]] = llvm.extractelement %arg0[%{{.*}} : i32] : vector<3xi32>
// CHECK: %[[A2:.*]] = llvm.insertvalue %[[E2]], %[[A1]][2]
// CHECK: %[[MPOISON:.*]] = llvm.mlir.poison : !llvm.struct<"feme.tight_vector", (array<3 x i32>)>
// CHECK: %[[MVAL:.*]] = llvm.insertvalue %[[A2]], %[[MPOISON]][0]
// CHECK: %[[V0:.*]] = llvm.insertvalue %[[MVAL]], %[[POISON]][0]
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

// -----

// Checks that `spirv.CompositeConstruct` building an array-of-struct value
// (roadmap L22, e.g. a tessellation-control-shader patch function's own
// `HSInput[3]`-shaped output, assembled from three already-converted
// `HSInput` struct values, one per control point) converts: MLIR has no
// pattern at all for an array-result `CompositeConstruct` (only the
// vector, struct, and matrix cases above were previously implemented),
// discovered by a real check-hlsl-feme-vk run
// (Feature/Semantics/HullSystemValues.test,
// Feature/Semantics/DomainSystemValues.test) failing
// `vkCreateGraphicsPipelines` with `"failed to legalize operation
// 'spirv.CompositeConstruct' that was explicitly marked illegal"`. Mirrors
// the matrix case exactly: lowers to an `llvm.mlir.poison` seed with one
// `llvm.insertvalue` per element, each already-converted whole-element
// constituent inserted as-is.

// CHECK-LABEL: llvm.func @construct_array_of_struct
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : !llvm.array<3 x struct<packed (vector<4xf32>)>>
// CHECK: %[[V0:.*]] = llvm.insertvalue %arg0, %[[POISON]][0]
// CHECK: %[[V1:.*]] = llvm.insertvalue %arg1, %[[V0]][1]
// CHECK: %[[V2:.*]] = llvm.insertvalue %arg2, %[[V1]][2]
// CHECK: llvm.return %[[V2]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @construct_array_of_struct(
      %c0 : !spirv.struct<HSInput, (vector<4xf32> [0])>,
      %c1 : !spirv.struct<HSInput, (vector<4xf32> [0])>,
      %c2 : !spirv.struct<HSInput, (vector<4xf32> [0])>)
      -> !spirv.array<3 x !spirv.struct<HSInput, (vector<4xf32> [0])>> "None" {
    %0 = spirv.CompositeConstruct %c0, %c1, %c2
        : (!spirv.struct<HSInput, (vector<4xf32> [0])>,
           !spirv.struct<HSInput, (vector<4xf32> [0])>,
           !spirv.struct<HSInput, (vector<4xf32> [0])>)
        -> !spirv.array<3 x !spirv.struct<HSInput, (vector<4xf32> [0])>>
    spirv.ReturnValue %0 : !spirv.array<3 x !spirv.struct<HSInput, (vector<4xf32> [0])>>
  }
}
