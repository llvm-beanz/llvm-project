// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// (Roadmap H148) A scalar-element `spirv.AccessChain` into a `RowMajor`
// matrix reached through a non-`Block` struct (e.g. `PushConstant`) --
// the shape `Feature/PushConstant/matrix.test` exercises -- used to
// forward its two trailing (column, then row) indices completely
// unchanged, unlike the analogous `Block`-backed case
// (spirv-to-llvm-matrix-block-scalar.mlir), whose own
// `rewriteBlockAccess` already reorders them correctly:
// `OffsetStructMemberReorderAccessChainPattern`, the pattern that
// legalizes this non-`Block`/non-handle access chain, only ever
// delegated indices past its own member selector to
// `remapNestedStructMemberIndices`, whose matrix handling only fires for
// a matrix discovered one level *inside* a further nested struct member
// -- never for a matrix that is itself the directly selected member, the
// common (and only, for a single-member push-constant struct) shape.
// `getPhysicalMatrixMemberType`'s own transposed-type substitution was
// already correct; only the access-chain's own index order into that
// type was wrong. `adjustMatrixScalarElementIndices`, now called
// directly from this pattern's own body whenever the selected member is
// itself a matrix, reorders/pads those two trailing indices exactly as
// `rewriteBlockAccess`'s own long-working scalar-element logic already
// does.

// CHECK-LABEL: llvm.func @read_scalar
// CHECK: %[[SCALAR:.*]] = llvm.getelementptr %{{.*}}[%{{.*}}, 0, %arg1, %arg0]
// CHECK: llvm.load %[[SCALAR]] : !llvm.ptr<13> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @pc : !spirv.ptr<!spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=8]) >, PushConstant>
  spirv.func @read_scalar(%col : i32, %row : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @pc : !spirv.ptr<!spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=8]) >, PushConstant>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %col, %row] : !spirv.ptr<!spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, RowMajor, MatrixStride=8]) >, PushConstant>, i32, i32, i32 -> !spirv.ptr<f32, PushConstant>
    %v = spirv.Load "PushConstant" %ac : f32
    spirv.ReturnValue %v : f32
  }
  spirv.EntryPoint "GLCompute" @read_scalar
  spirv.ExecutionMode @read_scalar "LocalSize", 1, 1, 1
}

// -----

// The same scalar-element shape, for a `ColMajor` padded-stride member
// (`MatrixStride=16`, exceeding the natural 8-byte,
// `sizeof(vector<2xf32>)` per-column stride) -- here `col` is the outer
// (major) index and `row` the inner (minor) one, the opposite order from
// the `RowMajor` case above, and an interior pad index (a static `0`)
// must additionally be inserted between them, matching how
// `getPhysicalMatrixMemberType`'s own major/minor choice and padding
// always follow the declared layout's own physical majorness/stride.

// CHECK-LABEL: llvm.func @read_scalar
// CHECK: %[[SCALAR:.*]] = llvm.getelementptr %{{.*}}[%{{.*}}, 0, %arg0, 0, %arg1]
// CHECK: llvm.load %[[SCALAR]] : !llvm.ptr<13> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @pc : !spirv.ptr<!spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, ColMajor, MatrixStride=16]) >, PushConstant>
  spirv.func @read_scalar(%col : i32, %row : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @pc : !spirv.ptr<!spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, ColMajor, MatrixStride=16]) >, PushConstant>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0, %col, %row] : !spirv.ptr<!spirv.struct<(!spirv.matrix<2 x vector<2xf32>> [0, ColMajor, MatrixStride=16]) >, PushConstant>, i32, i32, i32 -> !spirv.ptr<f32, PushConstant>
    %v = spirv.Load "PushConstant" %ac : f32
    spirv.ReturnValue %v : f32
  }
  spirv.EntryPoint "GLCompute" @read_scalar
  spirv.ExecutionMode @read_scalar "LocalSize", 1, 1, 1
}
