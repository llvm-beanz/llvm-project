; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap H8v: a bound `R32_SINT`/`R32_UINT` storage image
; (`target("spirv.Image", i32, ...)` with `Sampled == 2`, unlike
; `spirv-resource-lowering-image.ll`'s own sampled-image `Sampled == 1`)
; reached through `OpImageTexelPointer` + an atomic op --
; `feme::spirv::ImageTexelPointerPattern`/`AtomicRMWPattern`/
; `AtomicCompareExchangePattern` (`SPIRVToLLVMPatterns.cpp`) lower those to
; the same `getpointer` intrinsic every other storage-image access already
; uses, plus an ordinary LLVM `atomicrmw`/`cmpxchg` against it -- normalized
; here into the new `feme.cpu.image.atomic.*.2d.i32` calls (ImageCalls.h).

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define i32 @atomic_add(
; CHECK-SAME: i32 %x, i32 %y, i32 %value, ptr %resource_heap, i32 %resource_heap_count, ptr %sampler_heap, i32 %sampler_heap_count, ptr %root_constants, i32 %root_constant_size, ptr %image_heap, i32 %image_heap_count
define i32 @atomic_add(i32 %x, i32 %y, i32 %value) {
  %img = call target("spirv.Image", i32, 1, 0, 0, 0, 2, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 0, i32 1, i32 0, ptr null)
  %coord = insertelement <2 x i32> poison, i32 %x, i64 0
  %coord2 = insertelement <2 x i32> %coord, i32 %y, i64 1
  %ptr = call ptr @llvm.spv.resource.getpointer.timg(
      target("spirv.Image", i32, 1, 0, 0, 0, 2, 0) %img, <2 x i32> %coord2)
  ; CHECK: %[[X:.*]] = extractelement <2 x i32> %coord2, i64 0
  ; CHECK: %[[Y:.*]] = extractelement <2 x i32> %coord2, i64 1
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.image.atomic.add.2d.i32(ptr %image_heap, i32 %image_heap_count, i32 0, i32 %[[X]], i32 %[[Y]], i32 %value, i1 true)
  %old = atomicrmw add ptr %ptr, i32 %value seq_cst
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

; CHECK-LABEL: define i32 @atomic_umax(
define i32 @atomic_umax(i32 %x, i32 %y, i32 %value) {
  %img = call target("spirv.Image", i32, 1, 0, 0, 0, 2, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 1, i32 1, i32 0, ptr null)
  %coord = insertelement <2 x i32> poison, i32 %x, i64 0
  %coord2 = insertelement <2 x i32> %coord, i32 %y, i64 1
  %ptr = call ptr @llvm.spv.resource.getpointer.timg(
      target("spirv.Image", i32, 1, 0, 0, 0, 2, 0) %img, <2 x i32> %coord2)
  ; CHECK: call i32 @feme.cpu.image.atomic.umax.2d.i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 %{{.*}}, i32 %{{.*}}, i32 %value, i1 true)
  %old = atomicrmw umax ptr %ptr, i32 %value seq_cst
  ret i32 %old
}

; `OpAtomicCompareExchange` reaches LLVM IR as an `llvm.cmpxchg` plus an
; `extractvalue ..., 0` picking out the old value (see
; `AtomicCompareExchangePattern`'s own comment) -- both are rewritten into
; one `feme.cpu.image.atomic.compare_exchange.2d.i32` call, with the
; `extractvalue` itself erased since the new call's own result already is
; the old value.

; CHECK-LABEL: define i32 @atomic_compare_exchange(
define i32 @atomic_compare_exchange(i32 %x, i32 %y, i32 %comparator, i32 %value) {
  %img = call target("spirv.Image", i32, 1, 0, 0, 0, 2, 0)
      @llvm.spv.resource.handlefrombinding.timg(i32 0, i32 2, i32 1, i32 0, ptr null)
  %coord = insertelement <2 x i32> poison, i32 %x, i64 0
  %coord2 = insertelement <2 x i32> %coord, i32 %y, i64 1
  %ptr = call ptr @llvm.spv.resource.getpointer.timg(
      target("spirv.Image", i32, 1, 0, 0, 0, 2, 0) %img, <2 x i32> %coord2)
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.image.atomic.compare_exchange.2d.i32(ptr %image_heap, i32 %image_heap_count, i32 2, i32 %{{.*}}, i32 %{{.*}}, i32 %comparator, i32 %value, i1 true)
  %pair = cmpxchg ptr %ptr, i32 %comparator, i32 %value seq_cst seq_cst
  %old = extractvalue { i32, i1 } %pair, 0
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

declare target("spirv.Image", i32, 1, 0, 0, 0, 2, 0)
    @llvm.spv.resource.handlefrombinding.timg(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer.timg(
    target("spirv.Image", i32, 1, 0, 0, 0, 2, 0), <2 x i32>)

; Each atomic access reserves its own image-heap slot (bindings 0, 1, 2 in
; declaration order); no sampler is ever bound for a storage-image atomic.

; CHECK: !{!"atomic_add", i32 0, i1 false, i32 0, i32 0, i32 0}
; CHECK: !{!"atomic_umax", i32 0, i1 false, i32 0, i32 0, i32 0}
; CHECK: !{!"atomic_compare_exchange", i32 0, i1 false, i32 0, i32 0, i32 0}
