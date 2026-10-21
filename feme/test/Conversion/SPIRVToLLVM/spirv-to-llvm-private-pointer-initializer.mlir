// RUN: feme-opt --mlir-very-unsafe-disable-verifier-on-parsing --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// `dEQP-VK.spirv_assembly.instruction.compute.variable_init.private.
// {float,vec4}_from_workgroup`'s own shape: a `Private`-storage
// `spirv.GlobalVariable` whose pointee type is itself a `spirv.ptr`, and
// whose `initializer` is a `FlatSymbolRefAttr` naming another (here,
// `Workgroup`-storage) `spirv.GlobalVariable` directly, not a
// `spirv.SpecConstant`/`spirv.SpecConstantCompositeOp` as upstream MLIR's
// own `spirv.GlobalVariable` verifier otherwise requires of any
// `initializer` ("no support of initialization with a constant value" --
// see `GlobalVariablePattern`'s own comment). The SPIR-V deserializer
// builds this exact unverified shape directly (it never runs the
// `spirv.GlobalVariable` verifier on the ops it constructs), so this test
// parses with `--mlir-very-unsafe-disable-verifier-on-parsing` to admit
// it the same way, rather than attempting (and failing) to construct it
// through the normal, verifier-enforced textual parser.
//
// CHECK-LABEL: llvm.mlir.global external @wg
// CHECK-SAME: addr_space = 3
// CHECK: llvm.mlir.global private @priv_ptr
// CHECK-SAME: addr_space = 0
// CHECK-SAME: !llvm.ptr<3>
// CHECK: %[[ADDR:.*]] = llvm.mlir.addressof @wg : !llvm.ptr<3>
// CHECK: llvm.return %[[ADDR]] : !llvm.ptr<3>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, VariablePointers], []> {
  spirv.GlobalVariable @wg : !spirv.ptr<f32, Workgroup>
  spirv.GlobalVariable @priv_ptr initializer(@wg) : !spirv.ptr<!spirv.ptr<f32, Workgroup>, Private>
  spirv.func @read_priv_ptr() -> f32 "None" {
    %0 = spirv.mlir.addressof @priv_ptr : !spirv.ptr<!spirv.ptr<f32, Workgroup>, Private>
    %1 = spirv.Load "Private" %0 : !spirv.ptr<f32, Workgroup>
    %v = spirv.Load "Workgroup" %1 : f32
    spirv.ReturnValue %v : f32
  }
}
