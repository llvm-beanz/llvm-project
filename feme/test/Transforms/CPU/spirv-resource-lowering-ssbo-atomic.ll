; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap H8x: an ordinary storage buffer (`HandleKind::Storage`, a bound
; `spirv.VulkanBuffer` over a *runtime array*, `RWStructuredBuffer<int>` in
; HLSL / an `SSBO`'s own `buffer`-block runtime-array member in GLSL, e.g.
; `atomicAdd(ssbo.data[idx], 1)`) reached through a plain
; `OpAccessChain`-derived pointer -- *not* an `OpImageTexelPointer`, unlike
; spirv-resource-lowering-texel-buffer-atomic.ll's own `HandleKind::TexelStorage`
; case -- now lowers an `AtomicRMWInst`/`AtomicCmpXchgInst` the identical
; way that test's texel-buffer atomic does, just addressed by *byte offset*
; through the `Raw` call family (`feme.cpu.resource.atomic.*.raw.i32`)
; rather than by element index through the `Typed` family, mirroring how
; this same handle kind's own plain load/store already goes through
; `feme.cpu.resource.load.raw.*`/`store.raw.*` (see
; spirv-resource-lowering.ll). `hasOnlySupportedPointerUses`'s atomic gate
; widened from `Writable && IsTexel` to `Writable` alone is what newly
; allows this -- `Writable` was already exactly the
; `Storage`/`StorageStruct`/`TexelStorage` set an atomic is semantically
; valid against, so no other gating logic needed to change.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define i32 @atomic_add(
; CHECK-SAME: i32 %idx, i32 %value, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size
define i32 @atomic_add(i32 %idx, i32 %value) {
  %h = call target("spirv.VulkanBuffer", [0 x i32], 12, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x i32], 12, 1) %h, i32 %idx)
  ; CHECK: %[[IDX:.*]] = zext i32 %idx to i64
  ; CHECK: %[[OFF:.*]] = mul i64 %[[IDX]], 4
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.resource.atomic.add.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %[[OFF]], i32 %value, i1 true)
  %old = atomicrmw add ptr %ptr, i32 %value seq_cst
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

; CHECK-LABEL: define i32 @atomic_umax(
define i32 @atomic_umax(i32 %idx, i32 %value) {
  %h = call target("spirv.VulkanBuffer", [0 x i32], 12, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x i32], 12, 1) %h, i32 %idx)
  ; CHECK: call i32 @feme.cpu.resource.atomic.umax.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 %{{.*}}, i32 %value, i1 true)
  %old = atomicrmw umax ptr %ptr, i32 %value seq_cst
  ret i32 %old
}

; `OpAtomicCompareExchange` reaches LLVM IR as an `llvm.cmpxchg` plus an
; `extractvalue ..., 0` picking out the old value -- both are rewritten
; into one `feme.cpu.resource.atomic.compare_exchange.raw.i32` call, with
; the `extractvalue` itself erased since the new call's own result already
; is the old value, mirroring
; spirv-resource-lowering-texel-buffer-atomic.ll's own identical
; `atomic_compare_exchange` test.

; CHECK-LABEL: define i32 @atomic_compare_exchange(
define i32 @atomic_compare_exchange(i32 %idx, i32 %comparator, i32 %value) {
  %h = call target("spirv.VulkanBuffer", [0 x i32], 12, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 2, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x i32], 12, 1) %h, i32 %idx)
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.resource.atomic.compare_exchange.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 2, i64 %{{.*}}, i32 %comparator, i32 %value, i1 true)
  %pair = cmpxchg ptr %ptr, i32 %comparator, i32 %value seq_cst seq_cst
  %old = extractvalue { i32, i1 } %pair, 0
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

declare target("spirv.VulkanBuffer", [0 x i32], 12, 1)
    @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", [0 x i32], 12, 1), i32)

; Each atomic access reserves its own resource-heap slot (bindings 0, 1, 2
; in declaration order).

; CHECK: !{!"atomic_add", i32 0, i1 false, i32 0, i32 0, i32 0}
; CHECK: !{!"atomic_umax", i32 0, i1 false, i32 0, i32 0, i32 0}
; CHECK: !{!"atomic_compare_exchange", i32 0, i1 false, i32 0, i32 0, i32 0}
