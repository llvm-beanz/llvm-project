// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L227(a): a `spirv.Store`/`spirv.Load` carrying a Vulkan Memory
// Model availability/visibility bit (`MakePointerAvailable`,
// `MakePointerVisible`, or `NonPrivatePointer`) previously failed to
// legalize outright -- upstream's own default pattern only tolerates
// `Aligned`/`Volatile`/`Nontemporal` and explicitly marks every other bit
// illegal, and neither this project nor upstream had a pattern to widen
// that for these three bits. Real DXC output uses exactly this shape for
// a `groupshared`-array write immediately before an explicit
// `Barrier(GROUP_SHARED_MEMORY, GROUP_SCOPE | GROUP_SYNC)` call (see
// `Feature/HLSLLib/Barrier.32.test` in `offload-test-suite`). Since
// MLIR's own `spirv.Store`/`spirv.Load` ops do not model these bits' own
// paired scope operand at all (only `memory_access`/`alignment` -- see
// `SPIRVMemoryOps.td`), and the real synchronizes-with fence these bits
// ask for is already provided by the separate `spirv.ControlBarrier`/
// `spirv.MemoryBarrier` this project lowers to a genuine
// `llvm.spv.*_memory_barrier[_with_group_sync]` intrinsic, a plain load/
// store is a correct, conservative lowering: these bits convert exactly
// like a bare (no memory-access) load/store, just as `Aligned`/
// `Volatile`/`Nontemporal` alone need no special handling beyond the
// flags `llvm.load`/`llvm.store` already carry.

// CHECK-LABEL: llvm.func @store_make_pointer_available
// CHECK: llvm.store %{{.*}}, %{{.*}} : f32, !llvm.ptr<3>
// CHECK-NEXT: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.GlobalVariable @var : !spirv.ptr<f32, Workgroup>
  spirv.func @store_make_pointer_available(%arg0 : f32) -> () "None" {
    %0 = spirv.mlir.addressof @var : !spirv.ptr<f32, Workgroup>
    spirv.Store "Workgroup" %0, %arg0 ["MakePointerAvailable|NonPrivatePointer"] : f32
    spirv.Return
  }
}

// -----

// CHECK-LABEL: llvm.func @load_make_pointer_visible
// CHECK: %[[VAL:.*]] = llvm.load %{{.*}} : !llvm.ptr<3> -> f32
// CHECK-NEXT: llvm.return %[[VAL]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.GlobalVariable @var : !spirv.ptr<f32, Workgroup>
  spirv.func @load_make_pointer_visible() -> (f32) "None" {
    %0 = spirv.mlir.addressof @var : !spirv.ptr<f32, Workgroup>
    %1 = spirv.Load "Workgroup" %0 ["MakePointerVisible|NonPrivatePointer"] : f32
    spirv.ReturnValue %1 : f32
  }
}

// -----

// Combining a Vulkan Memory Model bit with a genuinely-modeled one
// (`Aligned`) still honors the latter -- this pattern only widens which
// bits are *tolerated*, it does not stop honoring the ones upstream's own
// pattern already applies.

// CHECK-LABEL: llvm.func @store_make_pointer_available_aligned
// CHECK: llvm.store %{{.*}}, %{{.*}} <alignment = 4> : f32, !llvm.ptr<3>
// CHECK-NEXT: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Linkage], []> {
  spirv.GlobalVariable @var : !spirv.ptr<f32, Workgroup>
  spirv.func @store_make_pointer_available_aligned(%arg0 : f32) -> () "None" {
    %0 = spirv.mlir.addressof @var : !spirv.ptr<f32, Workgroup>
    spirv.Store "Workgroup" %0, %arg0 ["MakePointerAvailable|NonPrivatePointer|Aligned", 4] : f32
    spirv.Return
  }
}
