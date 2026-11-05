// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a `spirv.SpecConstantComposite` constituent that is a
// `spirv.ModuleSpecConstantOperation` (the module-scope, named counterpart
// of `spirv.SpecConstantOperation`, used when an `OpSpecConstantOp` is only
// ever referenced as a composite constituent, never inline at a function-
// scope point of use -- e.g. `WorkgroupSize`'s own
// `gl_WorkGroupSize.x * gl_WorkGroupSize.y`) resolves to a concrete
// compile-time value via `prepareSpecConstants`'s own constant-folding of
// the wrapped op, the same way a plain `spirv.SpecConstant` constituent
// does (roadmap L7j): this ICD has no runtime `VkSpecializationInfo`
// override mechanism, so both of `spirv.ModuleSpecConstantOperation`'s
// operands (themselves spec constants or ordinary constants) are already
// fixed at compile time.

// CHECK-LABEL: llvm.func @workgroup_size_ref
// CHECK: llvm.mlir.constant(dense<[32, 4, 1]> : vector<3xi32>) : vector<3xi32>
// CHECK-NOT: spirv.mlir.referenceof
// CHECK-NOT: spirv.SpecConstant
// CHECK-NOT: spirv.ModuleSpecConstantOperation
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.SpecConstant @sc_x = 8 : i32
  spirv.SpecConstant @sc_y = 4 : i32
  spirv.SpecConstant @sc_z = 1 : i32
  %0 = spirv.mlir.referenceof @sc_x : i32
  %1 = spirv.Constant 4 : i32
  spirv.ModuleSpecConstantOperation @sco wraps "spirv.IMul"(%0, %1) : (i32, i32) -> i32
  spirv.SpecConstantComposite @scc_size (@sco, @sc_y, @sc_z) : vector<3xi32>
  spirv.func @workgroup_size_ref() -> vector<3xi32> "None" {
    %2 = spirv.mlir.referenceof @scc_size : vector<3xi32>
    spirv.ReturnValue %2 : vector<3xi32>
  }
}
