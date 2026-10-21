// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file %s

// Checks that a `spirv.mlir.referenceof` of a matrix-shaped
// `spirv.SpecConstantComposite` is declined (not approximated): no known
// real HLSL/CTS source needs this shape today, and `prepareSpecConstants`
// only resolves vector/struct/array-shaped composites (roadmap L7j), so
// this must remain illegal rather than silently miscompile. (A
// `spirv.struct`-shaped composite -- e.g. `dEQP-VK...opundef.
// undefined_spec_constant_composite`'s own `Pair = OpTypeStruct %int %int`
// spec constant -- is resolved like `spirv.Constant`'s own struct value,
// via `StructConstantPattern`, so it is not tested as invalid here.)

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Matrix], []> {
  spirv.SpecConstantComposite @scc_col0 (1.0 : f32, 0.0 : f32) : vector<2xf32>
  spirv.SpecConstantComposite @scc_col1 (0.0 : f32, 1.0 : f32) : vector<2xf32>
  spirv.SpecConstantComposite @scc_matrix (@scc_col0, @scc_col1) : !spirv.matrix<2 x vector<2xf32>>
  spirv.func @matrix_composite_ref() -> !spirv.matrix<2 x vector<2xf32>> "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.mlir.referenceof' that was explicitly marked illegal}}
    %0 = spirv.mlir.referenceof @scc_matrix : !spirv.matrix<2 x vector<2xf32>>
    spirv.ReturnValue %0 : !spirv.matrix<2 x vector<2xf32>>
  }
}
