// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H129 regression test: a struct whose members are declared out of
// physical (ascending-offset) order AND whose physically-first member's own
// natural size undershoots the gap to its physically-second member -- an
// *interior* gap, as opposed to the *leading* one
// spirv-to-llvm-push-constant-leading-gap.mlir already covers. Real
// `dEQP-VK.ubo.random.all_out_of_order_offsets.*` fuzz cases routinely
// declare a struct shaped like this one (found via this roadmap item's own
// investigation: `mat_cbuffer`/random-offset UBO struct with an `f32`
// declared after, but physically placed before, a larger member). Before
// this fix, `layOutStructIfOffsetsMatch` only ever padded a *leading* gap,
// so this struct's own conversion failed outright (and, cascading from
// there, so did every `spirv.AccessChain`/`spirv.mlir.addressof` into it).
//
// This first case is a plain (non-Block/handle, `PushConstant`-storage)
// struct, exercising `OffsetStructMemberReorderAccessChainPattern`'s own
// physical-index remap (via getStructMemberPhysicalIndex).
//
// Physical (ascending-offset) order is [i32 @0 (declared member 1), f32 @16
// (declared member 0)]: the i32's own natural 4-byte size leaves 12 bytes
// before the f32's declared offset that its own 4-byte alignment can never
// reach on its own, so this becomes `!llvm.struct<(i32, array<12 x i8>,
// f32)>` -- declared member 0 (f32) is real physical field 2, declared
// member 1 (i32) is real physical field 0.
// CHECK: llvm.mlir.global external constant @pc() {addr_space = 13 : i32} : !llvm.struct<(i32, array<12 x i8>, f32)>
// CHECK-LABEL: llvm.func @read_f32
// CHECK: %[[BASE0:.*]] = llvm.mlir.addressof @pc : !llvm.ptr<13>
// CHECK: %[[FIELD0:.*]] = llvm.getelementptr %[[BASE0]][%{{.*}}, 2]
// CHECK: llvm.load %[[FIELD0]] : !llvm.ptr<13> -> f32
// CHECK-LABEL: llvm.func @read_i32
// CHECK: %[[BASE1:.*]] = llvm.mlir.addressof @pc : !llvm.ptr<13>
// CHECK: %[[FIELD1:.*]] = llvm.getelementptr %[[BASE1]][%{{.*}}, 0]
// CHECK: llvm.load %[[FIELD1]] : !llvm.ptr<13> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @pc : !spirv.ptr<!spirv.struct<(f32 [16], i32 [0]), Block>, PushConstant>
  spirv.func @read_f32() -> f32 "None" {
    %0 = spirv.mlir.addressof @pc : !spirv.ptr<!spirv.struct<(f32 [16], i32 [0]), Block>, PushConstant>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0] : !spirv.ptr<!spirv.struct<(f32 [16], i32 [0]), Block>, PushConstant>, i32 -> !spirv.ptr<f32, PushConstant>
    %v = spirv.Load "PushConstant" %ac : f32
    spirv.ReturnValue %v : f32
  }
  spirv.func @read_i32() -> i32 "None" {
    %0 = spirv.mlir.addressof @pc : !spirv.ptr<!spirv.struct<(f32 [16], i32 [0]), Block>, PushConstant>
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1] : !spirv.ptr<!spirv.struct<(f32 [16], i32 [0]), Block>, PushConstant>, i32 -> !spirv.ptr<i32, PushConstant>
    %v = spirv.Load "PushConstant" %ac : i32
    spirv.ReturnValue %v : i32
  }
}

// -----

// The same reordered/interior-gapped struct shape, but as a `Uniform`
// buffer block's own content declared *directly* (no dxc-style single-
// member wrapper, see spirv-to-llvm-uniform-buffer.mlir's own two cases)
// -- exercises rewriteBlockAccess's own physical-index remap of the
// `llvm.spv.resource.getpointer` index instead, since this struct converts
// to a `spirv.VulkanBuffer` handle rather than an ordinary LLVM pointer.
// CHECK-LABEL: llvm.func @read_f32
// CHECK: %[[HANDLE0:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(i32, array<12 x i8>, f32)>, 2, 0>
// CHECK: %[[IDX0:.*]] = llvm.mlir.constant(2 : i32) : i32
// CHECK-NEXT: %[[FIELD0:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE0]], %[[IDX0]])
// CHECK: llvm.load %[[FIELD0]] : !llvm.ptr<12> -> f32
// CHECK-LABEL: llvm.func @read_i32
// CHECK: %[[HANDLE1:.*]] = llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(i32, array<12 x i8>, f32)>, 2, 0>
// CHECK: %[[IDX1:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK-NEXT: %[[FIELD1:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"(%[[HANDLE1]], %[[IDX1]])
// CHECK: llvm.load %[[FIELD1]] : !llvm.ptr<12> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 2) : !spirv.ptr<!spirv.struct<(f32 [16], i32 [0]), Block>, Uniform>
  spirv.func @read_f32() -> f32 "None" {
    %0 = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(f32 [16], i32 [0]), Block>, Uniform>
    %c0 = spirv.Constant 0 : i32
    %ac = spirv.AccessChain %0[%c0] : !spirv.ptr<!spirv.struct<(f32 [16], i32 [0]), Block>, Uniform>, i32 -> !spirv.ptr<f32, Uniform>
    %v = spirv.Load "Uniform" %ac : f32
    spirv.ReturnValue %v : f32
  }
  spirv.func @read_i32() -> i32 "None" {
    %0 = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(f32 [16], i32 [0]), Block>, Uniform>
    %c1 = spirv.Constant 1 : i32
    %ac = spirv.AccessChain %0[%c1] : !spirv.ptr<!spirv.struct<(f32 [16], i32 [0]), Block>, Uniform>, i32 -> !spirv.ptr<i32, Uniform>
    %v = spirv.Load "Uniform" %ac : i32
    spirv.ReturnValue %v : i32
  }
}
