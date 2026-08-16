; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; A divergent diamond nested inside another divergent diamond's true arm,
; both reconverging: the inner diamond flattens first (its own mask factors
; in the outer arm's mask, `%mask.t`), then the outer diamond flattens
; around the now-straight-line inner region. See "Phase 3: Linearization and
; Predication" in feme/docs/FeMeCPUDesign.md ("Divergent phis become selects
; of the incoming edge masks" -- nested `select`s compose through
; dominance without needing to be threaded explicitly).

; CHECK-LABEL: define void @main(
; CHECK: %c1 = icmp eq i32 %tid, 0
; CHECK: br label %outer.t
; CHECK: outer.t:
; CHECK: %c2 = icmp eq i32 %tid, 1
; CHECK: br label %inner.t
; CHECK: inner.t:
; CHECK-NEXT: %x1 = add i32 %tid, 10
; CHECK-NEXT: br label %inner.f
; CHECK: inner.f:
; CHECK-NEXT: %x2 = add i32 %tid, 20
; CHECK-NEXT: br label %outer.end
; CHECK: outer.end:
; CHECK: %inner.v.linearized = select i1 %c2, i32 %x1, i32 %x2
; CHECK-NEXT: br label %outer.f
; CHECK: outer.f:
; CHECK-NEXT: %x3 = add i32 %tid, 30
; CHECK-NEXT: br label %end
; CHECK: end:
; CHECK: %v.linearized = select i1 %c1, i32 %inner.v.linearized, i32 %x3
; CHECK-NOT: phi
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c1 = icmp eq i32 %tid, 0
  br i1 %c1, label %outer.t, label %outer.f
outer.t:
  %c2 = icmp eq i32 %tid, 1
  br i1 %c2, label %inner.t, label %inner.f
inner.t:
  %x1 = add i32 %tid, 10
  br label %outer.end
inner.f:
  %x2 = add i32 %tid, 20
  br label %outer.end
outer.end:
  %inner.v = phi i32 [%x1, %inner.t], [%x2, %inner.f]
  br label %end
outer.f:
  %x3 = add i32 %tid, 30
  br label %end
end:
  %v = phi i32 [%inner.v, %outer.end], [%x3, %outer.f]
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
