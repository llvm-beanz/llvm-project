; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L274 (previously milestone 9's own narrowing, see
; GroupShared.h's doc comment, before this fix): roadmap step R23 closed
; "an access through a getelementptr" for a first-level `getelementptr`
; (see simdize-groupshared-atomic-array.ll), but a *second*
; `getelementptr` off the first one -- a nested groupshared array-of-array
; access, e.g. `Shared2D[i][j]` -- used to still be diagnosed rather than
; canonicalized, since `rewriteGroupSharedGlobals`'s own validation pass
; only accepted a second-level `getelementptr` when the first level was a
; *divergent* vector-of-pointers row address (roadmap L11). This is one
; instance of the same shape `simdize-groupshared-nested-struct-array.ll`
; covers for a struct field's own nested array instead of an array's own
; nested array; both are now accepted by `isSupportedGroupSharedNestedGEPUser`'s
; fully general, depth- and type-unrestricted recursive check.

; CHECK-LABEL: define void @main(
; CHECK-SAME: ptr %wave_groupshared)
; CHECK-NOT: addrspace(3)
; CHECK: %shared.flat = getelementptr i8, ptr %wave_groupshared, i64 0
; CHECK-NEXT: %p1{{[0-9]*}} = getelementptr inbounds [2 x [4 x i32]], ptr %shared.flat, i32 0, i32 0
; CHECK-NEXT: %p2{{[0-9]*}} = getelementptr inbounds [4 x i32], ptr %p1{{[0-9]*}}, i32 0, i32 2
; CHECK-NEXT: %val = load i32, ptr %p2{{[0-9]*}}, align 4
define void @main() #0 {
  %p1 = getelementptr inbounds [2 x [4 x i32]], ptr addrspace(3) @shared, i32 0, i32 0
  %p2 = getelementptr inbounds [4 x i32], ptr addrspace(3) %p1, i32 0, i32 2
  %val = load i32, ptr addrspace(3) %p2
  ret void
}
@shared = internal addrspace(3) global [2 x [4 x i32]] undef
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
