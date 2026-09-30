; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L274, split out of L273's own atomic_operations triage: a
; *uniform* (compile-time-constant-indexed, non-divergent) access into a
; struct field's own nested array -- e.g. `buf.data.field[k]`, the shape
; glslang's whole-`AtomicStruct`-field struct-copy pattern
; (`shared struct { AtomicStruct data; } buf; ...; buf.data = result.data;`)
; compiles down to once its per-member SPIR-V `CompositeExtract`/
; `CompositeInsert` decomposition further decomposes an array-typed
; member into one access per element -- reaches its leaf `load`/`store`
; through *two* chained `getelementptr`s off the groupshared global (one
; selecting the struct field, a second indexing the array within it),
; not just the single level `simdize-groupshared-atomic-array.ll`'s own
; uniform-array case covers. `rewriteGroupSharedGlobals`'s own validation
; pass previously only accepted a second `getelementptr` level when the
; first was a *divergent*, already-widened vector-of-pointers row address
; (roadmap L11) -- rejecting this uniform, scalar-pointer nested chain
; outright with its own "feeds a nested getelementptr or another
; unsupported user" diagnostic, even though `retargetGroupSharedProducer`
; already rewrites a chain like this correctly (it recurses through every
; nested `getelementptr` level unconditionally, with no such
; restriction). Every `dEQP-VK.glsl.atomic_operations.*_{compute,mesh,
; task}_{shared,payload}` case (64 cases) hit this exact shape.
; `isSupportedGroupSharedNestedGEPUser` replaces the old single-level,
; vector-typed-only check with a fully general recursive one, accepting
; a uniform nested chain of any depth as long as every leaf is an
; ordinary supported access.

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK-NEXT: %inner{{[0-9]*}} = getelementptr inbounds { i32, [4 x i32] }, ptr %shared.flat, i32 0, i32 1
; CHECK-NEXT: %elt{{[0-9]*}} = getelementptr inbounds [4 x i32], ptr %inner{{[0-9]*}}, i32 0, i32 2
; CHECK-NEXT: %v = load i32, ptr %elt{{[0-9]*}}, align 4
; CHECK-NEXT: store i32 %v, ptr %elt{{[0-9]*}}, align 4
define void @main() #0 {
  %inner = getelementptr inbounds { i32, [4 x i32] }, ptr addrspace(3) @shared, i32 0, i32 1
  %elt = getelementptr inbounds [4 x i32], ptr addrspace(3) %inner, i32 0, i32 2
  %v = load i32, ptr addrspace(3) %elt
  store i32 %v, ptr addrspace(3) %elt
  ret void
}
@shared = internal addrspace(3) global { i32, [4 x i32] } undef
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
