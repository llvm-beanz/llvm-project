; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L20: a whole-struct load/store off a resource pointer -- no
; `getelementptr` navigating into an individual field at all, the shape
; `Feature/StructuredBuffer/packed.test`'s own
; `Doggo Fido = Buf[GI]; ...; Buf[GI] = Fido;` whole-struct-copy idiom
; produces -- is decomposed into one raw call per leaf field, reassembled
; with `insertvalue`/`extractvalue`, rather than left for
; `UnsupportedOps.cpp`'s generic diagnostic. `Doggo`'s own two vector
; fields (`int3 Legs`, `int2 Ears`) convert to fixed-size LLVM *arrays*
; (not LLVM vectors) once nested inside this tightly-packed struct, so
; this covers that nested-array shape too, not just a bare scalar struct.

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define void @whole_struct_copy(
; The load side: `Legs[0..2]` at byte offsets 0/4/8, `TailState` at 12,
; `Ears[0..1]` at 16/20, all reassembled with `insertvalue`.
; CHECK: %[[BASE:[0-9]+]] = mul i64 %{{[0-9]+}}, 24
; CHECK: %[[LEGS_BASE:[0-9]+]] = add i64 %[[BASE]], 0
; CHECK: %{{[0-9]+}} = add i64 %[[LEGS_BASE]], 0
; CHECK: call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %{{[0-9]+}}, i1 true)
; CHECK: %{{[0-9]+}} = add i64 %[[LEGS_BASE]], 4
; CHECK: call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %{{[0-9]+}}, i1 true)
; CHECK: %{{[0-9]+}} = add i64 %[[LEGS_BASE]], 8
; CHECK: call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %{{[0-9]+}}, i1 true)
; CHECK: %[[TAIL_OFF:[0-9]+]] = add i64 %[[BASE]], 12
; CHECK: call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %[[TAIL_OFF]], i1 true)
; CHECK: %[[EARS_BASE:[0-9]+]] = add i64 %[[BASE]], 16
; CHECK: %{{[0-9]+}} = add i64 %[[EARS_BASE]], 0
; CHECK: call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %{{[0-9]+}}, i1 true)
; CHECK: %{{[0-9]+}} = add i64 %[[EARS_BASE]], 4
; CHECK: call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %{{[0-9]+}}, i1 true)
; The store side: the reassembled struct is exploded back down with
; `extractvalue`, at the identical set of byte offsets.
; CHECK: %{{[0-9]+}} = extractvalue [3 x i32]
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %{{[0-9]+}}, i32 %{{[0-9]+}}, i1 true)
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %{{[0-9]+}}, i32 %{{[0-9]+}}, i1 true)
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %{{[0-9]+}}, i32 %{{[0-9]+}}, i1 true)
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %{{[0-9]+}}, i32 %{{[0-9]+}}, i1 true)
; CHECK: %{{[0-9]+}} = extractvalue [2 x i32]
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %{{[0-9]+}}, i32 %{{[0-9]+}}, i1 true)
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %{{[0-9]+}}, i32 %{{[0-9]+}}, i1 true)
define void @whole_struct_copy(i32 %idx) {
  %h = call target("spirv.VulkanBuffer", [0 x {[3 x i32], i32, [2 x i32]}], 12, 1)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 1, i32 1, i32 0, ptr null)
  %ptr = call ptr
      @llvm.spv.resource.getpointer(
          target("spirv.VulkanBuffer", [0 x {[3 x i32], i32, [2 x i32]}], 12, 1) %h, i32 %idx)
  %v = load {[3 x i32], i32, [2 x i32]}, ptr %ptr
  store {[3 x i32], i32, [2 x i32]} %v, ptr %ptr
  ret void
}
