// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file %s

// Checks that a `spirv.mlir.referenceof` of a `spirv.struct`-shaped
// `spirv.SpecConstantComposite` is declined (not approximated): no known
// real HLSL/CTS source needs this shape today, and `prepareSpecConstants`
// only resolves vector-shaped composites (roadmap L7j), so this must remain
// illegal rather than silently miscompile.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.SpecConstant @sc_i = 1 : i32
  spirv.SpecConstant @sc_f = 2.5 : f32
  spirv.SpecConstantComposite @scc_struct (@sc_i, @sc_f) : !spirv.struct<(i32, f32)>
  spirv.func @struct_composite_ref() -> !spirv.struct<(i32, f32)> "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.mlir.referenceof' that was explicitly marked illegal}}
    %0 = spirv.mlir.referenceof @scc_struct : !spirv.struct<(i32, f32)>
    spirv.ReturnValue %0 : !spirv.struct<(i32, f32)>
  }
}
