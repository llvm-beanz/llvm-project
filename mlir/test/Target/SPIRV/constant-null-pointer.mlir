// RUN: mlir-translate -no-implicit-module -test-spirv-roundtrip %s | FileCheck %s

// RUN: %if spirv-tools %{ rm -rf %t %}
// RUN: %if spirv-tools %{ mkdir %t %}
// RUN: %if spirv-tools %{ mlir-translate --no-implicit-module --serialize-spirv --spirv-save-validation-files-with-prefix=%t/module %s %}
// RUN: %if spirv-tools %{ spirv-val %t %}

// A pointer-typed `spirv.Constant` (`OpConstantNull` on a pointer type) is
// represented with a `UnitAttr` value (there is no meaningful payload to
// store). This round-trips through both the MLIR textual form and real
// SPIR-V binary (de)serialization.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, VariablePointers, Linkage], [SPV_KHR_storage_buffer_storage_class, SPV_KHR_variable_pointers]> {
  // CHECK-LABEL: spirv.func @null_pointer_constant
  spirv.func @null_pointer_constant() -> !spirv.ptr<!spirv.array<4xf32>, StorageBuffer> "None" {
    // CHECK: {{%.*}} = spirv.Constant unit : !spirv.ptr<!spirv.array<4 x f32>, StorageBuffer>
    %0 = spirv.Constant unit : !spirv.ptr<!spirv.array<4xf32>, StorageBuffer>
    spirv.ReturnValue %0 : !spirv.ptr<!spirv.array<4xf32>, StorageBuffer>
  }
}
