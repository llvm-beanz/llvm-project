// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.SpecConstantOperation` (roadmap H90) inlines cleanly:
// it has no lowering pattern of its own anywhere (neither upstream MLIR nor
// this project defines one), so without unwrapping it the conversion fails
// outright ("failed to legalize operation 'spirv.SpecConstantOperation'
// that was explicitly marked illegal"). Once a `spirv.mlir.referenceof` of
// the spec constant it wraps around has been resolved to that constant's
// own compile-time value (this ICD has no runtime `VkSpecializationInfo`
// override mechanism, roadmap L7j), the wrapped op (here `spirv.ISub`) is
// nothing more than an ordinary integer subtraction and legalizes exactly
// like any other -- confirmed via the real
// `dEQP-VK.mesh_shader.ext.properties.mesh_shared_memory_size` shader,
// whose own `const uint accessIdx = sharedMemoryElements - 1u - elemIdx;`
// (`sharedMemoryElements` a `constant_id` spec constant) is exactly this
// shape once compiled by glslang.

// CHECK-LABEL: llvm.func @spec_constant_operation_sub
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[SC:.*]] = llvm.mlir.constant(4 : i32) : i32
// CHECK: %[[SUB:.*]] = llvm.sub %[[SC]], %[[ONE]] : i32
// CHECK: llvm.return %[[SUB]]
// CHECK-NOT: spirv.SpecConstantOperation
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.SpecConstant @sc = 4 : i32
  spirv.func @spec_constant_operation_sub() -> i32 "None" {
    %one = spirv.Constant 1 : i32
    %ref = spirv.mlir.referenceof @sc : i32
    %sub = spirv.SpecConstantOperation wraps "spirv.ISub"(%ref, %one) : (i32, i32) -> i32
    spirv.ReturnValue %sub : i32
  }
}

// -----

// Checks a chain of two `spirv.SpecConstantOperation`s, the second using
// the first's own result as one of its operands -- confirms inlining one
// does not depend on processing order relative to the other (both get
// unwrapped regardless of which is inlined first, since replacing all uses
// of the first's result updates the second's operand transparently).

// CHECK-LABEL: llvm.func @spec_constant_operation_chain
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[SC:.*]] = llvm.mlir.constant(4 : i32) : i32
// CHECK: %[[SUB:.*]] = llvm.sub %[[SC]], %[[ONE]] : i32
// CHECK: %[[ADD:.*]] = llvm.add %[[SUB]], %[[ONE]] : i32
// CHECK: llvm.return %[[ADD]]
// CHECK-NOT: spirv.SpecConstantOperation
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.SpecConstant @sc = 4 : i32
  spirv.func @spec_constant_operation_chain() -> i32 "None" {
    %one = spirv.Constant 1 : i32
    %ref = spirv.mlir.referenceof @sc : i32
    %sub = spirv.SpecConstantOperation wraps "spirv.ISub"(%ref, %one) : (i32, i32) -> i32
    %add = spirv.SpecConstantOperation wraps "spirv.IAdd"(%sub, %one) : (i32, i32) -> i32
    spirv.ReturnValue %add : i32
  }
}
