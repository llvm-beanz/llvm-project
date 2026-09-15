// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file %s

// A `RowMajor` matrix's physical byte layout is transposed from the
// logical column-major type LLVM's own array-of-column-vectors
// representation always uses (see spirv-to-llvm-matrix-block.mlir's
// `ColMajor` case). Whole-matrix access (H124b) now handles this by
// transposing on load/store, but *partial* access -- selecting one
// logical column here, as below -- would need a genuine strided
// gather across `NumRows` physically non-adjacent elements, which
// this file does not yet implement; this is declined rather than
// silently miscompiled.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 5) : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, Uniform>
  spirv.func @read_column(%idx : i32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    // expected-error@+1 {{partial access (a row, column, or scalar element) into a matrix member whose declared RowMajor/MatrixStride layout is not yet supported}}
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, RowMajor, MatrixStride=16]), Block>, Uniform>, i32, i32 -> !spirv.ptr<vector<4xf32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<4xf32>
    spirv.ReturnValue %v : vector<4xf32>
  }
}

// -----

// A `MatrixStride` that does not match the natural per-column stride (48
// here, rather than the 16 bytes a `vector<4xf32>` column naturally takes)
// declares padding between columns. Whole-matrix access (H124b) now
// handles this via a padded physical substitution, but *partial*
// access -- selecting one logical column here, as below -- is still
// declined the same way as the `RowMajor` case above.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @ubo bind(0, 5) : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, ColMajor, MatrixStride=48]), Block>, Uniform>
  spirv.func @read_column(%idx : i32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @ubo : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, ColMajor, MatrixStride=48]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    // expected-error@+1 {{partial access (a row, column, or scalar element) into a matrix member whose declared RowMajor/MatrixStride layout is not yet supported}}
    %ac = spirv.AccessChain %0[%c0, %idx] : !spirv.ptr<!spirv.struct<(!spirv.matrix<4 x vector<4xf32>> [0, ColMajor, MatrixStride=48]), Block>, Uniform>, i32, i32 -> !spirv.ptr<vector<4xf32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<4xf32>
    spirv.ReturnValue %v : vector<4xf32>
  }
}
