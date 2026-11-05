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

// -----

// Checks that a `spirv.ModuleSpecConstantOperation` whose enclosed op has
// no `fold` implementation (e.g. a floating-point width conversion -- this
// project has only ever actually encountered integer arithmetic here, see
// `spirv-to-llvm-module-spec-constant-operation.mlir`) is declined, not
// approximated: `prepareSpecConstants` leaves it out of its resolved-value
// map, so any real reference to it (here, directly via
// `spirv.mlir.referenceof`) fails legalization instead of silently
// miscompiling.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.SpecConstant @sc0 = 1.0 : f32
  %0 = spirv.mlir.referenceof @sc0 : f32
  spirv.ModuleSpecConstantOperation @sco wraps "spirv.FConvert"(%0) : (f32) -> f64
  spirv.SpecConstantComposite @scc (@sco) : !spirv.array<1 x f64>
  spirv.func @unfoldable_module_spec_constant_operation() -> !spirv.array<1 x f64> "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.mlir.referenceof' that was explicitly marked illegal}}
    %1 = spirv.mlir.referenceof @scc : !spirv.array<1 x f64>
    spirv.ReturnValue %1 : !spirv.array<1 x f64>
  }
}
