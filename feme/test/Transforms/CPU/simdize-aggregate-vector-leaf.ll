; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L27: a divergent, whole *vector*-typed value (`%v`, built from two
; per-lane-divergent scalar resource loads) inserted as a single struct-field
; leaf via `insertvalue` (rather than each scalar component individually, the
; only shape roadmap L21 supported), then read back out via a constant-index
; `extractvalue` and stored through an ordinary, non-groupshared alloca --
; the exact shape reduced from a real `Feature/Semantics/HullSystemValues.
; test` failure, whose `PatchConstants` function builds a local per-control-
; point `float4 Position` scratch array (one per-control-point struct field
; holding a whole position vector) before indexing back into it by its own
; `SV_OutputControlPointID`. Previously diagnosed twice in a row: first as
; "has a divergent vector value ... used outside a supported ... pattern"
; (`%v`'s own `insertvalue` user was not an accepted consumer), then --
; after teaching `isSupportedAggregateLeafType` and `checkVectorDecomposition
; Supported`'s `insertvalue`-consumer branch about a whole vector leaf -- as
; the same diagnostic again (the vector `extractvalue` result's own
; `store` user was not an accepted consumer either). Both gaps are closed
; here: the struct's single `<2 x float>` leaf flattens into its own two
; `<W x float>` component slots (`countAggregateLeafScalars`/
; `flattenAggregateLeafScalarTypes`), `widenInsertValue`/`widenExtractValue`
; route a vector-typed insert/extract through `getVectorComponents` rather
; than a single flat scalar slot, and the ordinary `store` of the
; re-extracted divergent vector reaches the already-total
; `widenScalarizedFallback` (roadmap H6n's own per-lane vector-operand
; reassembly).

; CHECK-LABEL: define void @main(
; CHECK-COUNT-4: store <2 x float> {{.*}}, ptr {{.*}}, align 8
; CHECK: load <2 x float>, ptr %scratch, align 8
define void @main(ptr %resource_heap, i32 %resource_heap_count, ptr %scratch) #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %off = zext i32 %tid to i64
  %off1 = add i64 %off, 4

  %e0 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, i1 true)
  %e1 = call float @feme.cpu.resource.load.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off1, i1 true)
  %v.0 = insertelement <2 x float> poison, float %e0, i64 0
  %v = insertelement <2 x float> %v.0, float %e1, i64 1

  ; Insert the whole divergent vector as a single struct-field leaf.
  %s = insertvalue { <2 x float> } poison, <2 x float> %v, 0

  ; Read the vector leaf back out and stash it through an ordinary alloca
  ; -- the shape a hull shader's own per-control-point local scratch array
  ; takes when it stores an already-built struct field.
  %leaf = extractvalue { <2 x float> } %s, 0
  store <2 x float> %leaf, ptr %scratch, align 8

  %r = load <2 x float>, ptr %scratch, align 8
  %r0 = extractelement <2 x float> %r, i64 0
  %r1 = extractelement <2 x float> %r, i64 1
  call void @feme.cpu.resource.store.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, float %r0, i1 true)
  call void @feme.cpu.resource.store.raw.f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off1, float %r1, i1 true)
  ret void
}
declare float @feme.cpu.resource.load.raw.f32(ptr, i32, i32, i64, i1)
declare void @feme.cpu.resource.store.raw.f32(ptr, i32, i32, i64, float, i1)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
