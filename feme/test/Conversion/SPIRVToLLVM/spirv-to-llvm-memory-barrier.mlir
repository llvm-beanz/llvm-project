// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.MemoryBarrier` -- a real SPIR-V import's own
// `OpMemoryBarrier`, e.g. as glslang emits for a GLSL
// `subgroupMemoryBarrier()`-family call (roadmap L7l) -- converts directly
// to the plain (non-`_with_group_sync`) `llvm.spv.group.memory.barrier`
// intrinsic when its own `memory_scope` operand is `Workgroup`. Unlike
// `spirv.ControlBarrier`, this op has no `execution_scope` operand and so
// never implies any convergence requirement, hence the plain intrinsic
// rather than one of the three `_with_group_sync` variants.

// CHECK-LABEL: llvm.func @memory_barrier_workgroup
// CHECK: llvm.call_intrinsic "llvm.spv.group.memory.barrier"() : () -> ()
// CHECK-NEXT: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.func @memory_barrier_workgroup() -> () "None" {
    spirv.MemoryBarrier <Workgroup>, <WorkgroupMemory>
    spirv.Return
  }
}

// -----

// A `memory_scope` of exactly `Device` converts to the plain
// `llvm.spv.device.memory.barrier` intrinsic, a distinct granularity from
// both `Workgroup` and the conservative-superset `all` case below.

// CHECK-LABEL: llvm.func @memory_barrier_device
// CHECK: llvm.call_intrinsic "llvm.spv.device.memory.barrier"() : () -> ()
// CHECK-NEXT: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.func @memory_barrier_device() -> () "None" {
    spirv.MemoryBarrier <Device>, <CrossWorkgroupMemory|WorkgroupMemory>
    spirv.Return
  }
}

// -----

// A `memory_scope` this milestone's whole-group barrier support doesn't
// distinguish any further -- here, `Subgroup` -- converts to the widest
// `llvm.spv.all.memory.barrier` intrinsic instead: a safe superset fence,
// mirroring `ControlBarrierConversionPattern`'s own conservative-superset
// convention for the scopes it doesn't distinguish further either.

// CHECK-LABEL: llvm.func @memory_barrier_subgroup
// CHECK: llvm.call_intrinsic "llvm.spv.all.memory.barrier"() : () -> ()
// CHECK-NEXT: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.func @memory_barrier_subgroup() -> () "None" {
    spirv.MemoryBarrier <Subgroup>, <WorkgroupMemory>
    spirv.Return
  }
}
