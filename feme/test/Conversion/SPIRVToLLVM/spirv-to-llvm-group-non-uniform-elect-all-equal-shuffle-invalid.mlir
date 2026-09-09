// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file %s

// `ElectConversionPattern` (roadmap L7e) only implements `Subgroup`
// execution scope, mirroring `RotateConversionPattern` (roadmap F2):
// `Workgroup`-scope elect has no real HLSL/GLSL source in this ICD's
// frontend surface today.

spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform], []> {
  spirv.func @workgroup_elect() -> i1 "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.GroupNonUniformElect' that was explicitly marked illegal}}
    %0 = spirv.GroupNonUniformElect <Workgroup> : i1
    spirv.ReturnValue %0 : i1
  }
}

// -----

// `ShuffleConversionPattern` (roadmap L7e) only implements `Subgroup`
// execution scope, mirroring `RotateConversionPattern`/
// `ElectConversionPattern`.

spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformShuffle], []> {
  spirv.func @workgroup_shuffle(%value : f32, %id : i32) -> f32 "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.GroupNonUniformShuffle' that was explicitly marked illegal}}
    %0 = spirv.GroupNonUniformShuffle <Workgroup> %value, %id : f32, i32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// `AllEqualConversionPattern` (roadmap L7e) declines a *vector* operand:
// `spirv.GroupNonUniformAllEqualOp`'s result is always a single scalar
// `SPIRV_Bool` even for a vector `Value` (collapsing the whole vector into
// one true/false), a real semantic mismatch with `llvm.spv.wave.all_equal`'s
// own per-component vector-result shape that this pattern must not paper
// over -- see the pattern's own doc comment for the full reasoning.

spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformVote], []> {
  spirv.func @vector_all_equal(%value : vector<2xi32>) -> i1 "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.GroupNonUniformAllEqual' that was explicitly marked illegal}}
    %0 = spirv.GroupNonUniformAllEqual <Subgroup> %value : vector<2xi32>, i1
    spirv.ReturnValue %0 : i1
  }
}
