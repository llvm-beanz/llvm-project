// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file %s

// (Roadmap H129) Column-select and scalar-element access into a
// non-representable-layout matrix member reached *directly* as a struct
// member is now handled for both `RowMajor` and padded-stride `ColMajor`
// -- see spirv-to-llvm-matrix-block-column.mlir and
// spirv-to-llvm-matrix-block-scalar.mlir -- but getMatrixMemberLayout
// itself still declines a matrix member decorated `RowMajor` without a
// `MatrixStride` decoration at all (a malformed module: a matrix member
// always needs one to be laid out in memory in the first place, so a
// missing one cannot be resolved to any real address, regardless of the
// access shape) -- see getMatrixMemberLayout's own comment.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 5) : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor]), Block>, Uniform>
  spirv.func @read_column(%idx : i32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    // expected-error@+1 {{failed to legalize operation 'spirv.AccessChain' that was explicitly marked illegal}}
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor]), Block>, Uniform>, i32, i32 -> !spirv.ptr<vector<4xf32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<4xf32>
    spirv.ReturnValue %v : vector<4xf32>
  }
}
