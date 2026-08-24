// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file %s

// `RotateConversionPattern` (roadmap F2) only implements `Subgroup`
// execution scope: `Workgroup`-scope rotate has no real HLSL/GLSL source in
// this ICD's frontend surface and would need a different, shared-memory
// -based lowering this pattern does not provide, so it is declined rather
// than silently miscompiled.

spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformRotateKHR], [SPV_KHR_subgroup_rotate]> {
  spirv.func @workgroup_rotate(%value : f32, %delta : i32) -> f32 "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.GroupNonUniformRotateKHR' that was explicitly marked illegal}}
    %0 = spirv.GroupNonUniformRotateKHR <Workgroup> %value, %delta : f32, i32 -> f32
    spirv.ReturnValue %0 : f32
  }
}
