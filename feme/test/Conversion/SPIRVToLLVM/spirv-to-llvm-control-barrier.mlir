// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.ControlBarrier` -- a real SPIR-V import's own
// `OpControlBarrier` (as opposed to the `llvm.spv.*_memory_barrier
// [_with_group_sync]` intrinsic shape a DXIL/HLSL `GroupMemoryBarrier
// WithGroupSync()`-family call already produces) -- converts directly to
// the `llvm.spv.group.memory.barrier.with.group.sync` intrinsic when its
// own `memory_scope` operand is `Workgroup`, rather than falling through
// to upstream's own default pattern (a call to the mangled,
// un-lowerable-on-this-target `_Z22__spirv_ControlBarrieriii`, roadmap
// L44). `memory_semantics`'s own individual bits are not inspected: every
// control barrier converts the same way regardless of its exact ordering
// bits, since the SPIR-V spec requires convergence unconditionally.

// CHECK-LABEL: llvm.func @control_barrier_workgroup
// CHECK: llvm.call_intrinsic "llvm.spv.group.memory.barrier.with.group.sync"() : () -> ()
// CHECK-NEXT: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.func @control_barrier_workgroup() -> () "None" {
    spirv.ControlBarrier <Workgroup>, <Workgroup>, <WorkgroupMemory>
    spirv.Return
  }
}

// -----

// A `memory_scope` broader than `Workgroup` -- here, `Device` -- converts
// to the widest `llvm.spv.all.memory.barrier.with.group.sync` intrinsic
// instead: a safe superset fence, since this milestone's whole-group
// barrier support does not distinguish `Device` from `All` any further
// (see `feme::cpu::BarrierMemoryScope`'s own doc comment).

// CHECK-LABEL: llvm.func @control_barrier_device
// CHECK: llvm.call_intrinsic "llvm.spv.all.memory.barrier.with.group.sync"() : () -> ()
// CHECK-NEXT: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.func @control_barrier_device() -> () "None" {
    spirv.ControlBarrier <Workgroup>, <Device>, <CrossWorkgroupMemory|WorkgroupMemory>
    spirv.Return
  }
}

// -----

// A `memory_semantics` of `None` still converts to the narrowest `group`
// intrinsic (rather than being dropped, or erroring): `OpControlBarrier`'s
// own convergence requirement is unconditional, regardless of whether it
// also serves as a memory barrier (per the SPIR-V spec, "If Semantics is
// not None, this instruction also serves as an OpMemoryBarrier
// instruction ... If Semantics is None, Memory is ignored").

// CHECK-LABEL: llvm.func @control_barrier_no_memory_semantics
// CHECK: llvm.call_intrinsic "llvm.spv.group.memory.barrier.with.group.sync"() : () -> ()
// CHECK-NEXT: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.func @control_barrier_no_memory_semantics() -> () "None" {
    spirv.ControlBarrier <Workgroup>, <Workgroup>, <None>
    spirv.Return
  }
}
