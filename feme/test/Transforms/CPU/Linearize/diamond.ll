; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; A divergent if/else that reconverges immediately: the branch becomes
; unconditional fallthrough (into `t`, whose own former jump to `end` is
; redirected into `f` instead), and the `phi` merging the two arms' results
; becomes a `select` on the branch condition. See "Phase 3: Linearization
; and Predication" in feme/docs/FeMeCPUDesign.md.

; CHECK-LABEL: define void @main(
; CHECK: %c = icmp eq i32 %tid, 0
; CHECK: br label %t
; CHECK: t:
; CHECK-NEXT: %a = add i32 %tid, 1
; CHECK-NEXT: br label %f
; CHECK: f:
; CHECK-NEXT: %b = add i32 %tid, 2
; CHECK-NEXT: br label %end
; CHECK: end:
; CHECK: %v.linearized = select i1 %c, i32 %a, i32 %b
; CHECK-NOT: phi
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %f
t:
  %a = add i32 %tid, 1
  br label %end
f:
  %b = add i32 %tid, 2
  br label %end
end:
  %v = phi i32 [%a, %t], [%b, %f]
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
