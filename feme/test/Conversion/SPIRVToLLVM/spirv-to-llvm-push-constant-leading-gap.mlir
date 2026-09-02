// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H6q regression test: a push-constant block whose *first* member's
// own declared offset is nonzero -- real (`dxc`/glslang-compiled) SPIR-V
// produces this whenever the compiler or `spirv-opt`'s own dead-code
// elimination drops one or more members from the *front* of an interface
// block while every surviving member keeps its original byte offset
// relative to the whole block's own start (found in a real
// `dEQP-VK.mesh_shader.ext.api.draw.*with_task_shader*` task-stage module's
// own push-constant block, a `(i32 [12], i32 [16])` struct). Before the fix,
// `layOutStructIfOffsetsMatch` unconditionally started its cursor at 0,
// which can never match a nonzero first-member offset, so the whole
// `spirv.GlobalVariable` failed to legalize.
//
// The fix inserts a synthetic `[12 x i8]` leading padding member so the
// first real member lands at LLVM struct index 1, byte offset 12 --
// `OffsetStructLeadingPadAccessChainPattern` (SPIRVToLLVMPatterns.cpp)
// adds a matching `+1` to every `spirv.AccessChain` selecting a member of
// this struct, so `%c0`/`%c1` below become GEP indices 1/2, not 0/1.
// CHECK: llvm.mlir.global external constant @pc() {addr_space = 13 : i32} : !llvm.struct<(array<12 x i8>, i32, i32)>
// CHECK-LABEL: llvm.func @read_first
// CHECK: %[[BASE0:.*]] = llvm.mlir.addressof @pc : !llvm.ptr<13>
// CHECK: %[[FIELD0:.*]] = llvm.getelementptr %[[BASE0]][%{{.*}}, 1]
// CHECK: llvm.load %[[FIELD0]] : !llvm.ptr<13> -> i32
// CHECK-LABEL: llvm.func @read_second
// CHECK: %[[BASE1:.*]] = llvm.mlir.addressof @pc : !llvm.ptr<13>
// CHECK: %[[FIELD1:.*]] = llvm.getelementptr %[[BASE1]][%{{.*}}, 2]
// CHECK: llvm.load %[[FIELD1]] : !llvm.ptr<13> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @pc : !spirv.ptr<!spirv.struct<(i32 [12], i32 [16]), Block>, PushConstant>
  spirv.func @read_first() -> i32 "None" {
    %0 = spirv.mlir.addressof @pc : !spirv.ptr<!spirv.struct<(i32 [12], i32 [16]), Block>, PushConstant>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0] : !spirv.ptr<!spirv.struct<(i32 [12], i32 [16]), Block>, PushConstant>, i32 -> !spirv.ptr<i32, PushConstant>
    %v = spirv.Load "PushConstant" %ac : i32
    spirv.ReturnValue %v : i32
  }
  spirv.func @read_second() -> i32 "None" {
    %0 = spirv.mlir.addressof @pc : !spirv.ptr<!spirv.struct<(i32 [12], i32 [16]), Block>, PushConstant>
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1] : !spirv.ptr<!spirv.struct<(i32 [12], i32 [16]), Block>, PushConstant>, i32 -> !spirv.ptr<i32, PushConstant>
    %v = spirv.Load "PushConstant" %ac : i32
    spirv.ReturnValue %v : i32
  }
}

// -----

// A single-member struct isolates the leading-gap shape from any
// second-member concern: the whole struct is exactly the gap plus one
// real member, with nothing after it.
// CHECK: llvm.mlir.global external constant @pc() {addr_space = 13 : i32} : !llvm.struct<(array<12 x i8>, i32)>
// CHECK-LABEL: llvm.func @read_only
// CHECK: %[[BASE:.*]] = llvm.mlir.addressof @pc : !llvm.ptr<13>
// CHECK: %[[FIELD:.*]] = llvm.getelementptr %[[BASE]][%{{.*}}, 1]
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<13> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @pc : !spirv.ptr<!spirv.struct<(i32 [12]), Block>, PushConstant>
  spirv.func @read_only() -> i32 "None" {
    %0 = spirv.mlir.addressof @pc : !spirv.ptr<!spirv.struct<(i32 [12]), Block>, PushConstant>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0] : !spirv.ptr<!spirv.struct<(i32 [12]), Block>, PushConstant>, i32 -> !spirv.ptr<i32, PushConstant>
    %v = spirv.Load "PushConstant" %ac : i32
    spirv.ReturnValue %v : i32
  }
}
