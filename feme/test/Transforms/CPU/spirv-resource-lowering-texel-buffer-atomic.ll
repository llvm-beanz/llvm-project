; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap H8w: a bound `R32_SINT`/`R32_UINT` storage *texel buffer*
; (`target("spirv.Image", i32, 5, ...)` -- `Dim == 5` (Buffer), the same
; scalar-element shape spirv-resource-lowering-texel-buffer-scalar.ll's own
; load/store test uses, unlike spirv-resource-lowering-image-atomic.ll's
; own `Dim == 1` (2D) storage-*image* atomic) reached through
; `OpImageTexelPointer` + an atomic op lowers identically to that same
; storage-image atomic test: `feme::spirv::ImageTexelPointerPattern`/
; `AtomicRMWPattern`/`AtomicCompareExchangePattern` (`SPIRVToLLVMPatterns.cpp`)
; are `Dim`-agnostic, so they emit the same `getpointer` intrinsic here too
; -- only the CPU-side lowering (`SPIRVResourceLowering.cpp`) differs, since
; a texel buffer is addressed by a scalar element index rather than an
; (x, y) coordinate pair, and its own scalar-typed load/store already goes
; through the generic `feme.cpu.resource.*` family (`ResourceCalls.h`) built
; for exactly this handle kind (`HandleKind::TexelStorage`), rather than
; `feme.cpu.image.*` (`ImageCalls.h`).

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define i32 @atomic_add(
; CHECK-SAME: i32 %idx, i32 %value, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size
define i32 @atomic_add(i32 %idx, i32 %value) {
  %h = call target("spirv.Image", i32, 5, 0, 0, 0, 2, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 1) %h, i32 %idx)
  ; CHECK: %[[IDX:.*]] = zext i32 %idx to i64
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.resource.atomic.add.typed.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %[[IDX]], i32 %value, i1 true)
  %old = atomicrmw add ptr %ptr, i32 %value seq_cst
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

; CHECK-LABEL: define i32 @atomic_umax(
define i32 @atomic_umax(i32 %idx, i32 %value) {
  %h = call target("spirv.Image", i32, 5, 0, 0, 0, 2, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 1) %h, i32 %idx)
  ; CHECK: call i32 @feme.cpu.resource.atomic.umax.typed.i32(ptr %resource_heap, i32 %resource_heap_count, i32 1, i64 %{{.*}}, i32 %value, i1 true)
  %old = atomicrmw umax ptr %ptr, i32 %value seq_cst
  ret i32 %old
}

; `OpAtomicCompareExchange` reaches LLVM IR as an `llvm.cmpxchg` plus an
; `extractvalue ..., 0` picking out the old value (see
; `AtomicCompareExchangePattern`'s own comment) -- both are rewritten into
; one `feme.cpu.resource.atomic.compare_exchange.typed.i32` call, with the
; `extractvalue` itself erased since the new call's own result already is
; the old value, mirroring spirv-resource-lowering-image-atomic.ll's own
; identical `atomic_compare_exchange` test.

; CHECK-LABEL: define i32 @atomic_compare_exchange(
define i32 @atomic_compare_exchange(i32 %idx, i32 %comparator, i32 %value) {
  %h = call target("spirv.Image", i32, 5, 0, 0, 0, 2, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 2, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 1) %h, i32 %idx)
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.resource.atomic.compare_exchange.typed.i32(ptr %resource_heap, i32 %resource_heap_count, i32 2, i64 %{{.*}}, i32 %comparator, i32 %value, i1 true)
  %pair = cmpxchg ptr %ptr, i32 %comparator, i32 %value seq_cst seq_cst
  %old = extractvalue { i32, i1 } %pair, 0
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

declare target("spirv.Image", i32, 5, 0, 0, 0, 2, 1)
    @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer(target("spirv.Image", i32, 5, 0, 0, 0, 2, 1), i32)

; Each atomic access reserves its own resource-heap slot (bindings 0, 1, 2
; in declaration order).

; CHECK: !{!"atomic_add", i32 0, i1 false, i32 0, i32 0, i32 0}
; CHECK: !{!"atomic_umax", i32 0, i1 false, i32 0, i32 0, i32 0}
; CHECK: !{!"atomic_compare_exchange", i32 0, i1 false, i32 0, i32 0, i32 0}
