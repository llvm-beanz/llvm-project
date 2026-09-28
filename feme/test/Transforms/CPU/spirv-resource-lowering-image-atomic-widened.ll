; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L244: `spirv-resource-lowering-image-atomic.ll` covers the
; original `Plain2D`-only atomic shape (roadmap H8v); this file covers the
; four shapes L244 widened atomics to -- `Plain1D`, `Array1D`, `Array2D`
; (also reached by a plain storage `Cube`, folded into `Array2D` by
; `classifyStorageImage2DHandle`), and `Plain3D` -- one representative RMW
; op (`add`) plus `OpAtomicCompareExchange` per shape, mirroring
; `spirv-resource-lowering-image-fetch-lod.ll`'s own per-shape coordinate
; conventions (a bare scalar `i32` coordinate for `Plain1D`, no vector).

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define i32 @atomic_add_1d(
define i32 @atomic_add_1d(i32 %x, i32 %value) {
  %img = call target("spirv.Image", i32, 0, 0, 0, 0, 2, 0)
      @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr = call ptr @llvm.spv.resource.getpointer.timg1d(
      target("spirv.Image", i32, 0, 0, 0, 0, 2, 0) %img, i32 %x)
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.image.atomic.add.1d.i32(ptr %image_heap, i32 %image_heap_count, i32 0, i32 %x, i32 %value, i1 true)
  %old = atomicrmw add ptr %ptr, i32 %value seq_cst
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

; CHECK-LABEL: define i32 @atomic_compare_exchange_1d(
define i32 @atomic_compare_exchange_1d(i32 %x, i32 %comparator, i32 %value) {
  %img = call target("spirv.Image", i32, 0, 0, 0, 0, 2, 0)
      @llvm.spv.resource.handlefrombinding.timg1d(i32 0, i32 1, i32 1, i32 0, ptr null)
  %ptr = call ptr @llvm.spv.resource.getpointer.timg1d(
      target("spirv.Image", i32, 0, 0, 0, 0, 2, 0) %img, i32 %x)
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.image.atomic.compare_exchange.1d.i32(ptr %image_heap, i32 %image_heap_count, i32 1, i32 %x, i32 %comparator, i32 %value, i1 true)
  %pair = cmpxchg ptr %ptr, i32 %comparator, i32 %value seq_cst seq_cst
  %old = extractvalue { i32, i1 } %pair, 0
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

; CHECK-LABEL: define i32 @atomic_add_1d_array(
define i32 @atomic_add_1d_array(i32 %x, i32 %layer, i32 %value) {
  %img = call target("spirv.Image", i32, 0, 0, 1, 0, 2, 0)
      @llvm.spv.resource.handlefrombinding.timg1darray(i32 0, i32 2, i32 1, i32 0, ptr null)
  %coord = insertelement <2 x i32> poison, i32 %x, i64 0
  %coord2 = insertelement <2 x i32> %coord, i32 %layer, i64 1
  %ptr = call ptr @llvm.spv.resource.getpointer.timg1darray(
      target("spirv.Image", i32, 0, 0, 1, 0, 2, 0) %img, <2 x i32> %coord2)
  ; CHECK: %[[X:.*]] = extractelement <2 x i32> %coord2, i64 0
  ; CHECK: %[[LAYER:.*]] = extractelement <2 x i32> %coord2, i64 1
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.image.atomic.add.1darray.i32(ptr %image_heap, i32 %image_heap_count, i32 2, i32 %[[X]], i32 %[[LAYER]], i32 %value, i1 true)
  %old = atomicrmw add ptr %ptr, i32 %value seq_cst
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

; CHECK-LABEL: define i32 @atomic_compare_exchange_1d_array(
define i32 @atomic_compare_exchange_1d_array(i32 %x, i32 %layer, i32 %comparator, i32 %value) {
  %img = call target("spirv.Image", i32, 0, 0, 1, 0, 2, 0)
      @llvm.spv.resource.handlefrombinding.timg1darray(i32 0, i32 3, i32 1, i32 0, ptr null)
  %coord = insertelement <2 x i32> poison, i32 %x, i64 0
  %coord2 = insertelement <2 x i32> %coord, i32 %layer, i64 1
  %ptr = call ptr @llvm.spv.resource.getpointer.timg1darray(
      target("spirv.Image", i32, 0, 0, 1, 0, 2, 0) %img, <2 x i32> %coord2)
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.image.atomic.compare_exchange.1darray.i32(ptr %image_heap, i32 %image_heap_count, i32 3, i32 %{{.*}}, i32 %{{.*}}, i32 %comparator, i32 %value, i1 true)
  %pair = cmpxchg ptr %ptr, i32 %comparator, i32 %value seq_cst seq_cst
  %old = extractvalue { i32, i1 } %pair, 0
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

; CHECK-LABEL: define i32 @atomic_add_2d_array(
define i32 @atomic_add_2d_array(i32 %x, i32 %y, i32 %layer, i32 %value) {
  %img = call target("spirv.Image", i32, 1, 0, 1, 0, 2, 0)
      @llvm.spv.resource.handlefrombinding.timg2darray(i32 0, i32 4, i32 1, i32 0, ptr null)
  %coord = insertelement <3 x i32> poison, i32 %x, i64 0
  %coord2 = insertelement <3 x i32> %coord, i32 %y, i64 1
  %coord3 = insertelement <3 x i32> %coord2, i32 %layer, i64 2
  %ptr = call ptr @llvm.spv.resource.getpointer.timg2darray(
      target("spirv.Image", i32, 1, 0, 1, 0, 2, 0) %img, <3 x i32> %coord3)
  ; CHECK: %[[X:.*]] = extractelement <3 x i32> %coord3, i64 0
  ; CHECK: %[[Y:.*]] = extractelement <3 x i32> %coord3, i64 1
  ; CHECK: %[[LAYER:.*]] = extractelement <3 x i32> %coord3, i64 2
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.image.atomic.add.2darray.i32(ptr %image_heap, i32 %image_heap_count, i32 4, i32 %[[X]], i32 %[[Y]], i32 %[[LAYER]], i32 %value, i1 true)
  %old = atomicrmw add ptr %ptr, i32 %value seq_cst
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

; CHECK-LABEL: define i32 @atomic_compare_exchange_2d_array(
define i32 @atomic_compare_exchange_2d_array(i32 %x, i32 %y, i32 %layer, i32 %comparator, i32 %value) {
  %img = call target("spirv.Image", i32, 1, 0, 1, 0, 2, 0)
      @llvm.spv.resource.handlefrombinding.timg2darray(i32 0, i32 5, i32 1, i32 0, ptr null)
  %coord = insertelement <3 x i32> poison, i32 %x, i64 0
  %coord2 = insertelement <3 x i32> %coord, i32 %y, i64 1
  %coord3 = insertelement <3 x i32> %coord2, i32 %layer, i64 2
  %ptr = call ptr @llvm.spv.resource.getpointer.timg2darray(
      target("spirv.Image", i32, 1, 0, 1, 0, 2, 0) %img, <3 x i32> %coord3)
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.image.atomic.compare_exchange.2darray.i32(ptr %image_heap, i32 %image_heap_count, i32 5, i32 %{{.*}}, i32 %{{.*}}, i32 %{{.*}}, i32 %comparator, i32 %value, i1 true)
  %pair = cmpxchg ptr %ptr, i32 %comparator, i32 %value seq_cst seq_cst
  %old = extractvalue { i32, i1 } %pair, 0
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

; CHECK-LABEL: define i32 @atomic_add_3d(
define i32 @atomic_add_3d(i32 %x, i32 %y, i32 %z, i32 %value) {
  %img = call target("spirv.Image", i32, 2, 0, 0, 0, 2, 0)
      @llvm.spv.resource.handlefrombinding.timg3d(i32 0, i32 6, i32 1, i32 0, ptr null)
  %coord = insertelement <3 x i32> poison, i32 %x, i64 0
  %coord2 = insertelement <3 x i32> %coord, i32 %y, i64 1
  %coord3 = insertelement <3 x i32> %coord2, i32 %z, i64 2
  %ptr = call ptr @llvm.spv.resource.getpointer.timg3d(
      target("spirv.Image", i32, 2, 0, 0, 0, 2, 0) %img, <3 x i32> %coord3)
  ; CHECK: %[[X:.*]] = extractelement <3 x i32> %coord3, i64 0
  ; CHECK: %[[Y:.*]] = extractelement <3 x i32> %coord3, i64 1
  ; CHECK: %[[Z:.*]] = extractelement <3 x i32> %coord3, i64 2
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.image.atomic.add.3d.i32(ptr %image_heap, i32 %image_heap_count, i32 6, i32 %[[X]], i32 %[[Y]], i32 %[[Z]], i32 %value, i1 true)
  %old = atomicrmw add ptr %ptr, i32 %value seq_cst
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

; CHECK-LABEL: define i32 @atomic_compare_exchange_3d(
define i32 @atomic_compare_exchange_3d(i32 %x, i32 %y, i32 %z, i32 %comparator, i32 %value) {
  %img = call target("spirv.Image", i32, 2, 0, 0, 0, 2, 0)
      @llvm.spv.resource.handlefrombinding.timg3d(i32 0, i32 7, i32 1, i32 0, ptr null)
  %coord = insertelement <3 x i32> poison, i32 %x, i64 0
  %coord2 = insertelement <3 x i32> %coord, i32 %y, i64 1
  %coord3 = insertelement <3 x i32> %coord2, i32 %z, i64 2
  %ptr = call ptr @llvm.spv.resource.getpointer.timg3d(
      target("spirv.Image", i32, 2, 0, 0, 0, 2, 0) %img, <3 x i32> %coord3)
  ; CHECK: %[[OLD:.*]] = call i32 @feme.cpu.image.atomic.compare_exchange.3d.i32(ptr %image_heap, i32 %image_heap_count, i32 7, i32 %{{.*}}, i32 %{{.*}}, i32 %{{.*}}, i32 %comparator, i32 %value, i1 true)
  %pair = cmpxchg ptr %ptr, i32 %comparator, i32 %value seq_cst seq_cst
  %old = extractvalue { i32, i1 } %pair, 0
  ; CHECK: ret i32 %[[OLD]]
  ret i32 %old
}

declare target("spirv.Image", i32, 0, 0, 0, 0, 2, 0)
    @llvm.spv.resource.handlefrombinding.timg1d(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer.timg1d(
    target("spirv.Image", i32, 0, 0, 0, 0, 2, 0), i32)

declare target("spirv.Image", i32, 0, 0, 1, 0, 2, 0)
    @llvm.spv.resource.handlefrombinding.timg1darray(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer.timg1darray(
    target("spirv.Image", i32, 0, 0, 1, 0, 2, 0), <2 x i32>)

declare target("spirv.Image", i32, 1, 0, 1, 0, 2, 0)
    @llvm.spv.resource.handlefrombinding.timg2darray(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer.timg2darray(
    target("spirv.Image", i32, 1, 0, 1, 0, 2, 0), <3 x i32>)

declare target("spirv.Image", i32, 2, 0, 0, 0, 2, 0)
    @llvm.spv.resource.handlefrombinding.timg3d(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer.timg3d(
    target("spirv.Image", i32, 2, 0, 0, 0, 2, 0), <3 x i32>)
