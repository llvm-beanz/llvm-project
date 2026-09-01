; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L21: a divergent struct value built by a chain of `insertvalue`s
; from per-lane resource loads (mixing a whole sub-array, `[2 x i32]`,
; inserted at once with a genuine scalar leaf, `i32`), then read back apart
; by a chain of `extractvalue`s (again mixing a whole sub-array extraction
; with individual scalar-leaf extractions) feeding per-leaf resource
; stores -- the exact shape `feme::cpu::SPIRVResourceLoweringPass`'s own
; whole-aggregate resource load/store decomposition (roadmap L20) produces
; once reassembled through `feme::cpu::LinearizePass`, reduced from a real
; `Feature/StructuredBuffer/packed.test` failure down to this minimal IR.
; Each leaf now decomposes into its own `<4 x T>` (one `<W x T>` per
; flattened scalar leaf, "Vectors become components, not nested vectors"
; in "Phase 4: Widening", extended to aggregates) rather than the
; previously-diagnosed "of aggregate type" bail.

; CHECK-LABEL: define void @main(
; CHECK-COUNT-3: call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 {{%.*}}, i1 {{%.*}})
; CHECK-COUNT-3: call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 {{%.*}}, i32 {{%.*}}, i1 {{%.*}})
define void @main(ptr %resource_heap, i32 %resource_heap_count) #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %off = zext i32 %tid to i64

  %e0 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, i1 true)
  %off1 = add i64 %off, 4
  %e1 = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off1, i1 true)
  %off2 = add i64 %off, 8
  %scalar = call i32 @feme.cpu.resource.load.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off2, i1 true)

  ; Assemble `{[2 x i32], i32}` from the two array elements (each inserted
  ; as a genuine scalar leaf) and the trailing scalar field.
  %a0 = insertvalue [2 x i32] poison, i32 %e0, 0
  %a1 = insertvalue [2 x i32] %a0, i32 %e1, 1
  %s0 = insertvalue { [2 x i32], i32 } poison, [2 x i32] %a1, 0
  %s1 = insertvalue { [2 x i32], i32 } %s0, i32 %scalar, 1

  ; Read it back apart: a whole sub-array extraction (`%arr`, still
  ; aggregate-typed) followed by individual scalar-leaf extractions out of
  ; it, plus a direct scalar-leaf extraction for the trailing field.
  %arr = extractvalue { [2 x i32], i32 } %s1, 0
  %r0 = extractvalue [2 x i32] %arr, 0
  %r1 = extractvalue [2 x i32] %arr, 1
  %r2 = extractvalue { [2 x i32], i32 } %s1, 1

  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, i32 %r0, i1 true)
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off1, i32 %r1, i1 true)
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off2, i32 %r2, i1 true)
  ret void
}
declare i32 @feme.cpu.resource.load.raw.i32(ptr, i32, i32, i64, i1)
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
