// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a scalar `spirv.mlir.referenceof` of a `spirv.SpecConstant`
// resolves directly to that constant's own declared default value, since
// this ICD has no runtime `VkSpecializationInfo` override mechanism to make
// any other value meaningful (roadmap L7j).

// CHECK-LABEL: llvm.func @scalar_ref
// CHECK: llvm.mlir.constant(-5 : i32) : i32
// CHECK-NOT: spirv.mlir.referenceof
// CHECK-NOT: spirv.SpecConstant
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.SpecConstant @sc_int = -5 : i32
  spirv.func @scalar_ref() -> i32 "None" {
    %0 = spirv.mlir.referenceof @sc_int : i32
    spirv.ReturnValue %0 : i32
  }
}

// -----

// Checks the same for an *unsigned* scalar spec constant: its own declared
// value needs the same signed/unsigned -> signless retyping upstream's own
// `ConstantScalarAndVectorPattern` (for `spirv.Constant`) already applies,
// since SPIR-V's unsigned integer types and LLVM's signless ones otherwise
// mismatch when building an `llvm.mlir.constant`.

// CHECK-LABEL: llvm.func @unsigned_scalar_ref
// CHECK: llvm.mlir.constant(7 : i32) : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.SpecConstant @sc_uint = 7 : ui32
  spirv.func @unsigned_scalar_ref() -> ui32 "None" {
    %0 = spirv.mlir.referenceof @sc_uint : ui32
    spirv.ReturnValue %0 : ui32
  }
}

// -----

// Checks that a `spirv.mlir.referenceof` of a *vector-shaped*
// `spirv.SpecConstantComposite` (e.g. `gl_WorkGroupSize`'s own
// `vector<3xi32>` `LocalSizeId` composite, referencing three separately
// declared scalar `spirv.SpecConstant`s) resolves to a single
// `llvm.mlir.constant` built from each constituent's own resolved default
// value (roadmap L7j).

// CHECK-LABEL: llvm.func @vector_composite_ref
// CHECK: llvm.mlir.constant(dense<[4, 8, 1]> : vector<3xi32>) : vector<3xi32>
// CHECK-NOT: spirv.mlir.referenceof
// CHECK-NOT: spirv.SpecConstant
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.SpecConstant @sc_x = 4 : i32
  spirv.SpecConstant @sc_y = 8 : i32
  spirv.SpecConstant @sc_z = 1 : i32
  spirv.SpecConstantComposite @scc_size (@sc_x, @sc_y, @sc_z) : vector<3xi32>
  spirv.func @vector_composite_ref() -> vector<3xi32> "None" {
    %0 = spirv.mlir.referenceof @scc_size : vector<3xi32>
    spirv.ReturnValue %0 : vector<3xi32>
  }
}

// -----

// Checks the same for an unsigned vector composite (the exact
// `gl_WorkGroupSize`-shaped case that originally motivated L7j: dxc/glslang
// spell `LocalSizeId`'s three operands as `ui32` specialization constants).

// CHECK-LABEL: llvm.func @unsigned_vector_composite_ref
// CHECK: llvm.mlir.constant(dense<[16, 1, 1]> : vector<3xi32>) : vector<3xi32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.SpecConstant @sc_ux = 16 : ui32
  spirv.SpecConstant @sc_uy = 1 : ui32
  spirv.SpecConstant @sc_uz = 1 : ui32
  spirv.SpecConstantComposite @scc_usize (@sc_ux, @sc_uy, @sc_uz) : vector<3xui32>
  spirv.func @unsigned_vector_composite_ref() -> vector<3xui32> "None" {
    %0 = spirv.mlir.referenceof @scc_usize : vector<3xui32>
    spirv.ReturnValue %0 : vector<3xui32>
  }
}

