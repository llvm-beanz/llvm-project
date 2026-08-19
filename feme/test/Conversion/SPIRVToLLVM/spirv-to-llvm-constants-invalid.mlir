// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file %s

// An array of structs cannot flatten to a single `llvm.mlir.constant`
// `ElementsAttr` no matter how uniform its leaf scalar type is: LLVM's own
// element-count computation treats a `!llvm.struct` as a single opaque leaf
// (see `LLVM::ConstantOp::verify`), so an array-of-struct-of-array shape --
// exactly what an HLSL struct array compiles down to -- must be rejected by
// `ArrayConstantPattern` up front instead of building an `ElementsAttr` the
// verifier could never accept for it (which used to crash
// `DenseElementsAttr::get` outright with an incorrect leaf element type).

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @array_of_struct_of_array() -> !spirv.array<2 x !spirv.struct<(f32, !spirv.array<2 x f32>)>> "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.Constant' that was explicitly marked illegal}}
    %0 = spirv.Constant [
      [0.0 : f32, dense<[1.0, 2.0]> : tensor<2xf32>],
      [3.0 : f32, dense<[4.0, 5.0]> : tensor<2xf32>]
    ] : !spirv.array<2 x !spirv.struct<(f32, !spirv.array<2 x f32>)>>
    spirv.ReturnValue %0 : !spirv.array<2 x !spirv.struct<(f32, !spirv.array<2 x f32>)>>
  }
}
