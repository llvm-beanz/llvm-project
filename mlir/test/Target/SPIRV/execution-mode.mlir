// RUN: mlir-translate -no-implicit-module -split-input-file -test-spirv-roundtrip %s | FileCheck %s

// RUN: %if spirv-tools %{ rm -rf %t %}
// RUN: %if spirv-tools %{ mkdir %t %}
// RUN: %if spirv-tools %{ mlir-translate --no-implicit-module --serialize-spirv --split-input-file --spirv-save-validation-files-with-prefix=%t/module %s %}
// RUN: %if spirv-tools %{ spirv-val %t %}

spirv.module Logical OpenCL requires #spirv.vce<v1.0, [Kernel], []> {
  spirv.func @foo() -> () "None" {
    spirv.Return
  }
  spirv.EntryPoint "Kernel" @foo
  // CHECK: spirv.ExecutionMode @foo "LocalSizeHint", 3, 4, 5
  spirv.ExecutionMode @foo "LocalSizeHint", 3, 4, 5
}

// -----

// `DerivativeGroupLinearNV`/`DerivativeGroupQuadsNV` (numerically identical to
// their later `SPV_KHR_compute_shader_derivatives`-promoted `...KHR` names,
// per the SPIR-V spec's own extension-promotion convention of reusing the
// same enum value) are declared here under the real KHR extension a
// Vulkan 1.3+ compute shader actually emits -- `SPV_KHR_compute_shader_derivatives`
// itself had no `Extension` enum case at all until this fix (feme roadmap
// L60/L7), so any module declaring it via `OpExtension` failed to
// deserialize outright with "unknown extension", independent of the
// execution mode/capability's own already-recognized (by number) NV names.
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, ComputeDerivativeGroupLinearNV], [SPV_KHR_compute_shader_derivatives]> {
  spirv.func @foo() "None" {
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @foo
  spirv.ExecutionMode @foo "LocalSize", 4, 1, 1
  // CHECK: spirv.ExecutionMode @foo "DerivativeGroupLinearNV"
  spirv.ExecutionMode @foo "DerivativeGroupLinearNV"
}

