; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-resources -S %s | FileCheck %s

; Roadmap L16: `hasOnlySupportedUses`/`hasOnlySupportedPointerUses` never
; allowed a `getelementptr` past a `HandleKind::Uniform` (real read-only
; `cbuffer`) resource's own `llvm.spv.resource.getpointer` result --
; `AllowGEPs` was hard-coded to `Storage`/`StorageStruct` only, never
; `Uniform` -- so a struct-typed direct-field cbuffer member (`cbuffer
; CBStructs { X x1; X x2; }`, `X` a user-defined `{i32, i32}` struct, the
; exact shape `Feature/CBuffer/structs.test`'s own `x2.a2` access takes)
; hit `UnsupportedOps.cpp`'s generic "is a register-bound resource handle
; the FeMe CPU target cannot normalize" diagnostic once
; `feme::spirv::convertOffsetStructTypeIgnoringDecorations` (roadmap L13a)
; started legalizing the identified-struct-member shape at the
; SPIR-V-to-LLVM conversion layer -- an entirely distinct, later-phase gap
; from L13a's own scope. `AllowGEPs` now also covers `HandleKind::Uniform`,
; reusing the identical GEP-chain-to-byte-offset machinery
; `HandleKind::StorageStruct` (a direct-field *storage* block) already
; exercises; the second `cbuffer` field (`x2`, itself a 2-member struct,
; forcing a real nested `getelementptr` after `getpointer`'s own top-level
; field selection) resolves to the combined byte offset 20 (16, `x1`'s own
; padded struct size, plus 4, `.a2`'s own naturally-aligned offset within
; `X`).

target triple = "spirv-unknown-vulkan-compute"

; CHECK-LABEL: define i32 @main(
define i32 @main() {
  %h = call target("spirv.VulkanBuffer", {{i32, i32, [8 x i8]}, {i32, i32}}, 2, 0)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr1 = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {{i32, i32, [8 x i8]}, {i32, i32}}, 2, 0) %h, i32 0)
  %x1a1ptr = getelementptr inbounds {i32, i32, [8 x i8]}, ptr %ptr1, i32 0, i32 0
  %x1a1 = load i32, ptr %x1a1ptr
  ; CHECK: call i32 @feme.cpu.resource.load.raw.i32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 0, i1 true)

  %h2 = call target("spirv.VulkanBuffer", {{i32, i32, [8 x i8]}, {i32, i32}}, 2, 0)
      @llvm.spv.resource.handlefrombinding(i32 0, i32 0, i32 1, i32 0, ptr null)
  %ptr2 = call ptr
      @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {{i32, i32, [8 x i8]}, {i32, i32}}, 2, 0) %h2, i32 1)
  %x2a2ptr = getelementptr inbounds {i32, i32}, ptr %ptr2, i32 0, i32 1
  %x2a2 = load i32, ptr %x2a2ptr
  ; CHECK: call i32 @feme.cpu.resource.load.raw.i32(
  ; CHECK-SAME: ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 20, i1 true)

  %sum = add i32 %x1a1, %x2a2
  ret i32 %sum
}

declare target("spirv.VulkanBuffer", {{i32, i32, [8 x i8]}, {i32, i32}}, 2, 0)
    @llvm.spv.resource.handlefrombinding(i32, i32, i32, i32, ptr)
declare ptr @llvm.spv.resource.getpointer(target("spirv.VulkanBuffer", {{i32, i32, [8 x i8]}, {i32, i32}}, 2, 0), i32)

; CHECK-NOT: @llvm.spv.resource.handlefrombinding
