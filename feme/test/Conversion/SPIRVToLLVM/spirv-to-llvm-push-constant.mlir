// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a `PushConstant` block variable becomes an ordinary
// `llvm.mlir.global` in address space 13, the address space LLVM's own
// `SPIRVPushConstantAccess` pass recognizes and rewrites (together with
// every use) into the `spirv.PushConstant` handle representation and the
// `llvm.spv.pushconstant.getpointer` intrinsic itself -- so nothing further
// is needed here beyond routing the storage class to that address space
// (see `llvm/lib/Target/SPIRV/SPIRVPushConstantAccess.cpp`).

// CHECK: llvm.mlir.global external constant @pc() {addr_space = 13 : i32} : !llvm.struct<(f32, i32)>
// CHECK-LABEL: llvm.func @read_pc
// CHECK: %[[BASE:.*]] = llvm.mlir.addressof @pc : !llvm.ptr<13>
// CHECK: %[[FIELD:.*]] = llvm.getelementptr %[[BASE]][%{{.*}}, 0]
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<13> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @pc : !spirv.ptr<!spirv.struct<(f32 [0], i32 [4])>, PushConstant>
  spirv.func @read_pc() -> f32 "None" {
    %0 = spirv.mlir.addressof @pc : !spirv.ptr<!spirv.struct<(f32 [0], i32 [4])>, PushConstant>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0] : !spirv.ptr<!spirv.struct<(f32 [0], i32 [4])>, PushConstant>, i32 -> !spirv.ptr<f32, PushConstant>
    %v = spirv.Load "PushConstant" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

// -----

// A `Block`-decorated push-constant struct: real (`dxc`-compiled, or any
// binary-round-tripped) SPIR-V always decorates a push-constant/uniform
// block `Block`, with an explicit per-member `Offset` decoration -- unlike
// the hand-written module above, which omits both since MLIR's text parser
// does not require them. MLIR's own `convertStructTypeWithOffset`
// (`SPIRVToLLVM.cpp`) spuriously rejects this shape (see
// `convertOffsetStructTypeIgnoringDecorations`'s comment in
// SPIRVToLLVMPatterns.cpp for why), so this is the regression test for the
// fix: identical output to the case above, decoration aside.
// CHECK: llvm.mlir.global external constant @pc() {addr_space = 13 : i32} : !llvm.struct<(f32, i32)>
// CHECK-LABEL: llvm.func @read_pc
// CHECK: %[[BASE:.*]] = llvm.mlir.addressof @pc : !llvm.ptr<13>
// CHECK: %[[FIELD:.*]] = llvm.getelementptr %[[BASE]][%{{.*}}, 0]
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<13> -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @pc : !spirv.ptr<!spirv.struct<(f32 [0], i32 [4]), Block>, PushConstant>
  spirv.func @read_pc() -> f32 "None" {
    %0 = spirv.mlir.addressof @pc : !spirv.ptr<!spirv.struct<(f32 [0], i32 [4]), Block>, PushConstant>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0] : !spirv.ptr<!spirv.struct<(f32 [0], i32 [4]), Block>, PushConstant>, i32 -> !spirv.ptr<f32, PushConstant>
    %v = spirv.Load "PushConstant" %ac : f32
    spirv.ReturnValue %v : f32
  }
}

