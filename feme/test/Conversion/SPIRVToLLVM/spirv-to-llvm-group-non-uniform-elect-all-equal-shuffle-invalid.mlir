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

// `spirv.GroupNonUniformAllOp`/`AnyOp` (roadmap L7i) have no `Workgroup`
// negative-scope test here: per `SPIRVNonUniformOps.td`'s own
// `SPIRV_ExecutionScopeAttrIs<"execution_scope", ["Subgroup"]>` trait,
// `Workgroup` scope is rejected by the dialect's own verifier before this
// pattern ever runs, so `VoteConversionPattern` has no scope check of its
// own to exercise (see its doc comment for the full reasoning).

// -----

// `ShuffleXorConversionPattern` (roadmap L7i) only implements `Subgroup`
// execution scope, mirroring `ShuffleConversionPattern`/
// `ElectConversionPattern`.

spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformShuffle], []> {
  spirv.func @workgroup_shuffle_xor(%value : f32, %mask : i32) -> f32 "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.GroupNonUniformShuffleXor' that was explicitly marked illegal}}
    %0 = spirv.GroupNonUniformShuffleXor <Workgroup> %value, %mask : f32, i32
    spirv.ReturnValue %0 : f32
  }
}
